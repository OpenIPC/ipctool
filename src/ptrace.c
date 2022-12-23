#ifdef __arm__
#include "ptrace.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <mtd/mtd-abi.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "chipid.h"
#include "hal/common.h"
#include "hal/hisi/hal_hisi.h"
#include "hal/hisi/ptrace.h"
#include "hal/xm.h"
#include "hashtable.h"

#define ASSERT_PTRACE                                                          \
    if (ret == -1 && errno) {                                                  \
        switch (errno) {                                                       \
        case 3:                                                                \
            fprintf(stderr, "No such PID %d\n", proc->pid);                    \
            break;                                                             \
        default:                                                               \
            fprintf(stderr, "[%d] errno %d (%s)\n", proc->pid, errno,          \
                    strerror(errno));                                          \
        }                                                                      \
        exit(0);                                                               \
    }

#define IS_PREFIX(name, substr) (!strncmp(name, substr, sizeof substr - 1))

#define SSP_READ_ALT 0x1
#define SSP_WRITE_ALT 0X3

#define LINE "=========================="

struct process;
typedef void (*read_enter_hook)(struct process *proc, int fd, size_t buf,
                                size_t nbyte);
typedef void (*read_exit_hook)(struct process *proc, int fd, size_t buf,
                               size_t nbyte, ssize_t sysret);
typedef void (*write_enter_hook)(struct process *proc, int fd, size_t buf,
                                 size_t nbyte);
typedef void (*write_exit_hook)(struct process *proc, int fd, size_t buf,
                                size_t nbyte, ssize_t sysret);
typedef void (*ioctl_enter_hook)(struct process *proc, int fd, unsigned int cmd,
                                 size_t arg);
typedef void (*ioctl_exit_hook)(struct process *proc, int fd, unsigned int cmd,
                                size_t arg, ssize_t sysret);

#define CHECK_FD                                                               \
    if (fd < 0 || fd >= MAX_MON_FDS)                                           \
        return;

typedef struct {
    char *str;
    size_t ref_cnt;
} arc_str_t;

static const char *arc_cstr(arc_str_t *file) {
    if (!file)
        return NULL;

    return file->str;
}

typedef struct {
    arc_str_t *file;

    read_enter_hook read_enter;
    read_exit_hook read_exit;

    write_enter_hook write_enter;
    write_exit_hook write_exit;

    ioctl_enter_hook ioctl_enter;
    ioctl_exit_hook ioctl_exit;
} mon_fd_t;

typedef struct process {
    pid_t pid;
    struct user regs;
    size_t syscall_num;
#define MAX_MON_FDS 1024
    mon_fd_t fds[MAX_MON_FDS];
} process_t;

HashTable pids;

static void dump_regs(struct user const *regs, FILE *outfp) {
    fprintf(outfp, "r0   = 0x%08x, r1 = 0x%08x\n", regs->regs.uregs[0],
            regs->regs.uregs[1]);
    fprintf(outfp, "r2   = 0x%08x, r3 = 0x%08x\n", regs->regs.uregs[2],
            regs->regs.uregs[3]);
    fprintf(outfp, "r4   = 0x%08x, r5 = 0x%08x\n", regs->regs.uregs[4],
            regs->regs.uregs[5]);
    fprintf(outfp, "r6   = 0x%08x, r7 = 0x%08x\n", regs->regs.uregs[6],
            regs->regs.uregs[7]);
    fprintf(outfp, "r8   = 0x%08x, r9 = 0x%08x\n", regs->regs.uregs[8],
            regs->regs.uregs[9]);
}

/*
void linux_new_mempeek() {
  // Build iovec structs
  struct iovec local[1];
  local[0].iov_base = calloc(len, sizeof(char));
  local[0].iov_len = len;

  struct iovec remote[1];
  remote[0].iov_base = remoteptr;
  remote[0].iov_len = len;

  ssize_t nread = process_vm_readv(child, local, 2, remote, 1, 0);
}*/

// https://chromium.googlesource.com/chromiumos/docs/+/master/constants/syscalls.md#arm-32_bit_EABI
#define SYSCALL_READ 3
#define SYSCALL_WRITE 4
#define SYSCALL_OPEN 5
#define SYSCALL_CLOSE 6
#define SYSCALL_IOCTL 54
#define SYSCALL_NANOSLEEP 0xa2
#define SYSCALL_OPENAT 322

static void *copy_from_process(pid_t child, size_t addr, void *ptr,
                               size_t size) {
    size_t *buf = ptr;
    errno = 0;
    for (size_t i = 0; i < size; i += sizeof(size_t)) {
        long ret = ptrace(PTRACE_PEEKTEXT, child, addr + i, 0);
        if (ret == -1 && errno) {
            printf("error copy_from_process from %#x (%s)\n", addr,
                   strerror(errno));
            return NULL;
        }
        buf[i / sizeof(size_t)] = ret;
    }
    return ptr;
}

static char *copy_from_process_str(process_t *proc, size_t addr) {
    long ret;
    ssize_t buflen = 1024;
    char *buf = malloc(buflen);
    ssize_t readlen = 0;

    errno = 0;
    do {
        if (buflen == readlen) {
            buflen *= 2;
            buf = realloc(buf, buflen);
            assert(buf);
        }
        ret = ptrace(PTRACE_PEEKTEXT, proc->pid, addr + readlen, 0);
        if (ret == -1 && errno) {
            printf("error copy_from_process_str from %#x (%s)\n", addr,
                   strerror(errno));
            free(buf);
            return NULL;
        }
        *(size_t *)(buf + readlen) = ret;
        readlen += sizeof(size_t);
    } while (!memchr(&ret, 0, sizeof(ret)));
    return buf;
}

static void xm_i2c_change_addr(int new_addr) {
    static int old_addr;
    if (old_addr != new_addr) {
        printf("sensor_i2c_change_addr(%#x)\n", new_addr);
        old_addr = new_addr;
    }
}

static void xm_decode_i2c_read(pid_t child, uint32_t arg, ssize_t sysret) {
    I2C_DATA_S i2c_data = {0};

    void *ret = copy_from_process(child, arg, &i2c_data, sizeof(i2c_data));
    if (ret == NULL)
        return;

    xm_i2c_change_addr(i2c_data.dev_addr);
    printf("sensor_read_register(%#x); /* ", i2c_data.reg_addr);
    if (sysret != -1) {
        printf("-> %#x", i2c_data.data);
    } else {
        printf("[err]");
    }
    printf(" */\n");
}

static void xm_decode_i2c_write(pid_t child, uint32_t arg, ssize_t sysret) {
    I2C_DATA_S i2c_data = {0};

    void *ret = copy_from_process(child, arg, &i2c_data, sizeof(i2c_data));
    if (ret == NULL)
        return;

    xm_i2c_change_addr(i2c_data.dev_addr);
    printf("sensor_write_register(%#x, %#x);\n", i2c_data.reg_addr,
           i2c_data.data);
}

static void ssp_decode_read(int phase, pid_t child, uint32_t arg,
                            ssize_t sysret) {
    uint32_t value;

    void *ret = copy_from_process(child, arg, &value, sizeof(value));
    if (ret == NULL)
        return;

    static uint16_t addr;
    if (phase == 1) {
        addr = value >> 8;
    } else if (phase == 2) {
        printf("ssp_read_register(%#x); /* -> %#x */\n", addr, value & 0xff);
    }
}

static void ssp_decode_write(pid_t child, uint32_t arg, ssize_t sysret) {
    uint32_t value;

    void *ret = copy_from_process(child, arg, &value, sizeof(value));
    if (ret == NULL)
        return;

    printf("ssp_write_register(%#x, %#x);\n", value >> 8, value & 0xff);
}

static void hisi_decode_sns_read(int phase, pid_t child, uint32_t arg,
                                 ssize_t sysret) {
    struct i2c_rdwr_ioctl_data rdwr = {0};
    static uint16_t reg_addr;
    static size_t msg0_buf;
    unsigned char buf[4] = {0};

    if (phase == 1) {
        void *ret = copy_from_process(child, arg, &rdwr, sizeof(rdwr));
        if (ret == NULL)
            return;

        struct i2c_msg msg[2] = {0};
        ret = copy_from_process(child, (size_t)rdwr.msgs, msg, sizeof(msg));
        if (ret == NULL)
            return;

        msg0_buf = (size_t)msg[0].buf;
        ret = copy_from_process(child, msg0_buf, buf, sizeof(buf));
        if (ret == NULL)
            return;

        reg_addr = buf[0];
        if (msg[0].len == 2) {
            reg_addr <<= 8;
            reg_addr |= buf[1];
        }
    } else if (phase == 2) {
        void *ret = copy_from_process(child, msg0_buf, buf, sizeof(buf));
        if (ret == NULL)
            return;

        printf("sensor_read_register(%#hx); /* ", reg_addr);
        if (sysret == 2) {
            printf("-> %#x", buf[0]);
        } else {
            printf("[err]");
        }
        printf(" */\n");
    }
}

static unsigned char ioctl_arg[8];

static void hisi_i2c_read_enter_cb(process_t *proc, int fd, unsigned int cmd,
                                   size_t arg) {
    if (cmd == I2C_RDWR)
        hisi_decode_sns_read(1, proc->pid, arg, 0);
}

static void xm_i2c_ioctl_enter_cb(process_t *proc, int fd, unsigned int cmd,
                                  size_t arg) {
    if (cmd == CMD_I2C_WRITE)
        xm_decode_i2c_write(proc->pid, arg, 0);
}

static void ssp_ioctl_enter_cb(process_t *proc, int fd, unsigned int cmd,
                               size_t arg) {
    if (cmd == SSP_READ_ALT)
        ssp_decode_read(1, proc->pid, arg, 0);
}

static void mtd_ioctl_enter_cb(process_t *proc, int fd, unsigned int cmd,
                               size_t arg) {
    copy_from_process(proc->pid, arg, ioctl_arg, sizeof(ioctl_arg));
}

static void null_i2c_ioctl_exit_cb(process_t *proc, int fd, unsigned int cmd,
                                   size_t arg, ssize_t sysret) {}

static void dump_i2c_ioctl_exit_cb(process_t *proc, int fd, unsigned int cmd,
                                   size_t arg, ssize_t sysret) {
    if (proc->fds[fd].file)
        printf("ioctl_i2c('%s', 0x%x, 0x%x)\n", arc_cstr(proc->fds[fd].file),
               cmd, arg);
}

static void null_ioctl_exit_cb(process_t *proc, int fd, unsigned int cmd,
                               size_t arg, ssize_t sysret) {}

static void dump_ioctl_exit_cb(process_t *proc, int fd, unsigned int cmd,
                               size_t arg, ssize_t sysret) {
    if (proc->fds[fd].file)
        printf("ioctl('%s'(%d), 0x%x, 0x%x)\n", arc_cstr(proc->fds[fd].file),
               fd, cmd, arg);
}

typedef struct {
    int handle;
    int group;
    int num;
} XMGpioDescriptor_t;

static void xm_gpio_open(process_t *proc, size_t arg) {
    XMGpioDescriptor_t desc;

    void *ret = copy_from_process(proc->pid, arg, &desc, sizeof(desc));

    printf("XM_GPIO_REQUEST(GPIO%d_%d) = [%#x]\n", desc.group, desc.num,
           desc.handle);
}

static void xm_gpio_req(process_t *proc, size_t arg, const char *op) {
    uint32_t d[2] = {0};

    void *ret = copy_from_process(proc->pid, arg, &d, sizeof(d));
    printf("%s([%#x], %#x)\n", op, d[0], d[1]);
}

static void xm_gpio_ioctl_exit_cb(process_t *proc, int fd, unsigned int cmd,
                                  size_t arg, ssize_t sysret) {
    switch (cmd) {
    case 0xC0084705:
        return xm_gpio_req(proc, arg, "XM_GPIO_READ");
    case 0x40084706:
        return xm_gpio_req(proc, arg, "XM_GPIO_WRITE");
    case 0x401C4701:
        return xm_gpio_open(proc, arg);
    case 0x40084704:
        return xm_gpio_req(proc, arg, "XM_GPIO_DIRECTION_SET");
    case 0x40044709:
        puts("XM_GPIO_PRINT()");
        break;
    case 0x4004470A:
        puts("XM_DEMUX_CLEAR()");
        break;
    default:
        dump_ioctl_exit_cb(proc, fd, cmd, arg, 0);
    }
}

static void syscall_ioctl_enter(process_t *proc) {
    int fd = proc->regs.regs.uregs[0];
    CHECK_FD;

    unsigned int cmd = proc->regs.regs.uregs[1];
    size_t arg = proc->regs.regs.uregs[2];

    if (proc->fds[fd].ioctl_enter)
        proc->fds[fd].ioctl_enter(proc, fd, cmd, arg);
}

static void enter_syscall(process_t *proc) {
    memset(&proc->regs, 0, sizeof(proc->regs));
    long ret = ptrace(PTRACE_GETREGS, proc->pid, NULL, &proc->regs);
    ASSERT_PTRACE;

    proc->syscall_num = proc->regs.regs.uregs[7];
    switch (proc->syscall_num) {
    case SYSCALL_IOCTL: {
        syscall_ioctl_enter(proc);
        break;
    }
    default:;
    }
}

static size_t get_syscall_ret(process_t *proc) {
    struct user regs;
    memset(&regs, 0, sizeof(regs));
    long ret = ptrace(PTRACE_GETREGS, proc->pid, NULL, &regs);
    ASSERT_PTRACE;
    return regs.regs.uregs[0];
}

static void xm_i2c_ioctl_exit_cb(process_t *proc, int fd, unsigned int cmd,
                                 size_t arg, ssize_t sysret) {
    switch (cmd) {
    case I2C_RDWR:
        hisi_decode_sns_read(2, proc->pid, arg, sysret);
        break;
    case CMD_I2C_READ:
        xm_decode_i2c_read(proc->pid, arg, sysret);
        break;
    }
}

static void hisi_i2c_read_exit_cb(process_t *proc, int fd, unsigned int cmd,
                                  size_t arg, ssize_t sysret) {
    switch (cmd) {
    case I2C_SLAVE_FORCE:
        printf("sensor_i2c_change_addr(0x%x);\n", arg << 1);
        break;
    case I2C_RDWR:
        hisi_decode_sns_read(2, proc->pid, arg, sysret);
        break;
    }
}

static void ssp_ioctl_exit_cb(process_t *proc, int fd, unsigned int cmd,
                              size_t arg, ssize_t sysret) {
    switch (cmd) {
    case SSP_READ_ALT:
        ssp_decode_read(2, proc->pid, arg, sysret);
        break;
    case SSP_WRITE_ALT:
        ssp_decode_write(proc->pid, arg, sysret);
        break;
    }
}

static void dump_hisi_read_mipi(pid_t child, unsigned int cmd,
                                size_t remote_addr) {
    size_t stsize = hisi_sizeof_combo_dev_attr();
    char *buf = alloca(stsize);
    if (!copy_from_process(child, remote_addr, buf, stsize))
        return;

    hisi_dump_combo_dev_attr(buf, cmd);
}

static void hisi_mipi_ioctl_exit_cb(process_t *proc, int fd, unsigned int cmd,
                                    size_t arg, ssize_t sysret) {
    switch (cmd) {
    case HI_MIPI_RESET_MIPI:
    case HI_MIPI_RESET_SENSOR:
    case HI_MIPI_UNRESET_MIPI:
    case HI_MIPI_UNRESET_SENSOR:
    case HI_MIPI_SET_HS_MODE:
    case HI_MIPI_ENABLE_MIPI_CLOCK:
    case HI_MIPI_ENABLE_SENSOR_CLOCK:
        break;
    case HIV2X_MIPI_SET_DEV_ATTR:
    case HIV3A_MIPI_SET_DEV_ATTR:
    case HIV3_HI_MIPI_SET_DEV_ATTR:
    case HIV4A_MIPI_SET_DEV_ATTR:
    case HIV4_MIPI_SET_DEV_ATTR:
        dump_hisi_read_mipi(proc->pid, cmd, arg);
        break;
    default:
        printf("ERR: uknown cmd %#x for himipi\n", cmd);
    }
}

static void hisi_gen2_read_exit_cb(process_t *proc, int fd, size_t remote_addr,
                                   size_t nbyte, ssize_t sysret) {
    unsigned char *buf = alloca(nbyte);
    copy_from_process(proc->pid, remote_addr, buf, nbyte);
    // reg_width
    if (nbyte == 2) {
        printf("i2c_read() = 0x%x\n", *(u_int16_t *)buf);
    } else {
        printf("i2c_read() = 0x%x\n", *(u_int8_t *)buf);
    }
}

static void default_read_exit_cb(process_t *proc, int fd, size_t remote_addr,
                                 size_t nbyte, ssize_t sysret) {
#if 0
    printf("read(%d, ..., %d)\n", fd, nbyte);
#endif
}

static void default_write_exit_cb(process_t *proc, int fd, size_t remote_addr,
                                  size_t nbyte, ssize_t sysret) {
#if 0
    printf("write(%d, ..., %d)\n", fd, nbyte);
#endif
}

static void i2c_write_exit_cb(process_t *proc, int fd, size_t remote_addr,
                              size_t nbyte, ssize_t sysret) {
    unsigned char *buf = alloca(nbyte);
    void *res = copy_from_process(proc->pid, remote_addr, buf, nbyte);
    if (!res) {
        printf("ERROR: write(%d, 0x%x, %d) -> read from addrspace\n", fd,
               remote_addr, nbyte);
        return;
    }
    u_int16_t addr = buf[0] << 8 | buf[1];
    u_int8_t val = buf[2];
    printf("sensor_write_register(0x%x, 0x%x);\n", addr, val);
}

static void gpio_write_cb(process_t *proc, int fd, size_t remote_addr,
                          size_t nbyte, ssize_t sysret) {
    if (proc->fds[fd].file && nbyte > 0) {
        unsigned char *buf = alloca(nbyte) + 1;
        char *res = copy_from_process(proc->pid, remote_addr, buf, nbyte);
        if (!res) {
            printf("ERROR: gpio_write(%d, 0x%x, %d) -> read from addrspace\n",
                   fd, remote_addr, nbyte);
            return;
        }
        buf[nbyte] = 0;
        printf("gpio_write(%s, %s)\n", arc_cstr(proc->fds[fd].file), res);
    }
}

static void mtd_write_cb(process_t *proc, int fd, size_t remote_addr,
                         size_t nbyte, ssize_t sysret) {
    printf("mtd_write(%d, %zu, 0x%x)\n", fd, remote_addr, nbyte);
}

static void print_args(uint32_t *b, uint32_t *a, int cnt) {
    for (int i = 0; i < cnt; i++) {
        if (*b == *a)
            printf(", 0x%x", *(b + i));
        else
            printf(", 0x%x -> 0x%x", *(b + i), *(a + i));
    }
}

static void add_flag(char *buf, const char *name) {
    if (*buf)
        strcat(buf, " | ");
    strcat(buf, name);
}

static const char *spi_modes(uint32_t value, char buf[1024]) {
    if (value & SPI_CPHA) {
        add_flag(buf, "SPI_CPHA");
        value &= ~SPI_CPHA;
    }
    if (value & SPI_CPOL) {
        add_flag(buf, "SPI_CPOL");
        value &= ~SPI_CPOL;
    }
    if (value & SPI_LSB_FIRST) {
        add_flag(buf, "SPI_LSB_FIRST");
        value &= ~SPI_LSB_FIRST;
    }
    if (value & SPI_CS_HIGH) {
        add_flag(buf, "SPI_LSB_FIRST");
        value &= ~SPI_CS_HIGH;
    }
    if (value) {
        if (*buf)
            strcat(buf, " | ");
        sprintf(buf + strlen(buf), "%d", value);
    }
    return buf;
}

static void print_ioctl_spi(process_t *proc, int fd, size_t arg,
                            const char *name) {
    uint32_t d = 0;
    copy_from_process(proc->pid, arg, &d, sizeof(d));
    if (proc->fds[fd].file)
        printf("ioctl_spi('%s', %s, &%d);\n", arc_cstr(proc->fds[fd].file),
               name, d);
}

static void dump_an41908a_reg(const char *prefix, const char *op,
                              uint8_t reg_num, uint16_t value) {
    switch (reg_num) {
    case 0:
        printf("%s(IRS_TGT %s %#x)\n", prefix, op, value);
        break;
    case 0x21: {
        printf("%s(%#x %s %#x /* FZTEST %#x, TESTEN 2 %d */)\n", prefix,
               reg_num, op, value, value & 0x1f, value >> 7);
    } break;
    case 0x20: {
        uint8_t h = value >> 8 & 0xff;
        printf("%s(%#x %s %#x /* DT1 %#x, PWMMODE %#x, PWMRES %#x */)\n",
               prefix, reg_num, op, value, value & 0xf, h & 0x1f, h >> 5);
    } break;
    case 0x25: {
        const char *sf = reg_num == 0x25 ? "AB" : "CD";
        printf("%s(INTCT%s %s %#x)\n", prefix, sf, op, value);
    } break;
    case 0x22:
    case 0x27: {
        const char *sf = reg_num == 0x22 ? "AB" : "CD";
        const char pp = reg_num == 0x22 ? 'A' : 'C';
        uint8_t h = value >> 8 & 0xff;
        printf("%s(DT2%c %s %#x, PHMOD%s %s %#x)\n", prefix, pp, op,
               value & 0xff, sf, op, h);
    } break;
    case 0x23:
    case 0x28: {
        const char l = reg_num == 0x23 ? 'A' : 'C';
        const char r = reg_num == 0x23 ? 'B' : 'D';
        uint8_t h = value >> 8 & 0xff;
        printf("%s(PPW%c %s %#x, PPWA%c %s %#x)\n", prefix, l, op, value & 0xff,
               r, op, h);
    } break;
    case 0x24:
    case 0x29: {
        const char *sf = reg_num == 0x24 ? "AB" : "CD";
        const char led = reg_num == 0x24 ? 'B' : 'C';
        uint8_t h = value >> 8 & 0xff;
        printf(
            "%s(%#x %s %#x /* PSUM%s 0x%.2x, CCWCW%s %d, BRAKE%s %d, ENDIS%s "
            "%d, LED%c %d, MICRO%s %d */)\n",
            prefix, reg_num, op, value, sf, value & 0xff, sf, h & 1, sf,
            (h >> 1) & 1, sf, (h >> 2) & 1, led, (h >> 3) & 1, sf, h >> 4);
    } break;
    default:
        printf("%s(%#x %s %#x)\n", prefix, reg_num, op, value);
    }
}

#define MAX_AN41908A_REG 0x2A
static void an41908a_ioctl_exit_cb(process_t *proc, int fd, unsigned int cmd,
                                   size_t arg, ssize_t sysret) {
    if (SPI_IOC_MESSAGE(1) != cmd)
        return;

    struct spi_ioc_transfer mesg[1] = {0};
    copy_from_process(proc->pid, arg, &mesg[0], sizeof(mesg));
    if (mesg[0].len % 3 != 0) {
        printf("an41908a() -> errored len %d\n", mesg[0].len);
        return;
    }

    static uint16_t state[MAX_AN41908A_REG + 1];

    char *tx_buf = NULL, *rx_buf = NULL;
    if (mesg[0].len) {
        tx_buf = alloca(mesg[0].len);
        copy_from_process(proc->pid, mesg[0].tx_buf, tx_buf, mesg[0].len);
    }

    for (size_t i = 0; i < mesg[0].len; i += 3) {
        uint8_t reg_num = tx_buf[i] & 0x3f;
        uint16_t value = tx_buf[i + 1] | tx_buf[i + 2] << 8;
        if (reg_num > MAX_AN41908A_REG) {
            printf("an41908a(), bad reg_num %#x\n", reg_num);
        } else if (tx_buf[i] & 0x40) {
            if (rx_buf == NULL) {
                rx_buf = alloca(mesg[0].len);
                copy_from_process(proc->pid, mesg[0].rx_buf, rx_buf,
                                  mesg[0].len);
            }
            value = rx_buf[i + 1] | rx_buf[i + 2] << 8;
            if (value != state[reg_num])
                dump_an41908a_reg("read_an41908a", "->", reg_num, value);
        } else if (value != state[reg_num]) {
            dump_an41908a_reg("write_an41908a", "<-", reg_num, value);
            state[tx_buf[i]] = value;
        }
    }
}

static void spi_ioctl_exit_cb(process_t *proc, int fd, unsigned int cmd,
                              size_t arg, ssize_t sysret) {
    (void)sysret;

    switch (cmd) {
    case SPI_IOC_WR_MAX_SPEED_HZ:
        print_ioctl_spi(proc, fd, arg, "SPI_IOC_WR_MAX_SPEED_HZ");
        return;
    case SPI_IOC_WR_BITS_PER_WORD:
        print_ioctl_spi(proc, fd, arg, "SPI_IOC_WR_BITS_PER_WORD");
        return;
    case SPI_IOC_WR_MODE: {
        uint32_t d = 0;
        copy_from_process(proc->pid, arg, &d, sizeof(d));
        printf("ioctl_spi('%s', SPI_IOC_WR_MODE, &(%s));\n",
               arc_cstr(proc->fds[fd].file), spi_modes(d, (char[1024]){0}));
    }
        return;
    case SPI_IOC_MESSAGE(1): {
        struct spi_ioc_transfer mesg[1] = {0};
        copy_from_process(proc->pid, arg, &mesg[0], sizeof(mesg));
        char *tx_buf = NULL;
        if (mesg[0].len) {
            tx_buf = alloca(mesg[0].len);
            copy_from_process(proc->pid, mesg[0].tx_buf, tx_buf, mesg[0].len);
        }

        printf("ioctl_spi('%s', SPI_IOC_MESSAGE(1), { ",
               arc_cstr(proc->fds[fd].file));
        for (size_t i = 0; i < mesg[0].len; i++) {
            printf("%s%#x", i != 0 ? ", " : "", tx_buf[i]);
        }
        printf(" });\n");
    } break;
    default: {
        uint32_t d = 0;
        copy_from_process(proc->pid, arg, &d, sizeof(d));
        printf("ioctl_spi('%s', %#x, &%d);\n", arc_cstr(proc->fds[fd].file),
               cmd, d);
    }
    }
}

static const char *mtd_cmd_params(unsigned int cmd, int *args) {
    switch (cmd) {
    case 0x40044DA2u:
        return "XMMTD_CHECKPASSWD";
    case 0x40044DA1u:
        return "XMMTD_SETPASSWD";
    case 0x40084DA8:
    case 0x40044DA8u:
        return "XMMTD_UNLOCKUSER";
    case 0x40044DAAu:
        return "XMMTD_GETFLASHNAME";
    case 0x40044DA9u:
        return "XMMTD_GETFLASHID";
    case 0x40044da0:
        return "XMMTD_GETLOCKVERSION";
    case 0x40084DA6u:
        *args = 2;
        return "Flash_UnLock";
    case 0x40084d02:
        *args = 2;
        return "Flash_Erase";
    case 0x40084DA5u:
        *args = 2;
        return "Flash_Lock";
    case 0x40044DBFu:
        return "XMMTD_GETPROTECTFLAG";
    case 0x40044DBDu:
        return "XMMTD_PROTECTDISABLED";
    case 0x40044DBEu:
        return "XMMTD_SETPROTECTFLAG";
    default:
        return "?";
    }
}

static void mtd_ioctl_exit_cb(process_t *proc, int fd, unsigned int cmd,
                              size_t arg, ssize_t sysret) {
    uint32_t d[2] = {0};
    copy_from_process(proc->pid, arg, &d, sizeof(d));
    int num = 1;
    printf("mtd_ioctl('%s'(%d), %s (0x%x)", arc_cstr(proc->fds[fd].file), fd,
           mtd_cmd_params(cmd, &num), cmd);
    print_args((uint32_t *)ioctl_arg, d, num);
    printf(") = %d\n", sysret);
}

static void show_i2c_banner(int fd) {
    static int last_i2c_fd;
    if (last_i2c_fd != fd) {
        printf("%s i2c-%d %s\n", LINE, fd, LINE);
        last_i2c_fd = fd;
    }
}

static arc_str_t *new_arc_str(char *str) {
    arc_str_t *item = malloc(sizeof(*item));
    item->ref_cnt = 1;
    item->str = str;

    return item;
}

static void delete_arc_str(arc_str_t *item) {
    item->ref_cnt--;
    if (item->ref_cnt == 0) {
        free(item->str);
        free(item);
    }
}

static void free_fds(process_t *proc) {
    for (int i = 0; i < MAX_MON_FDS; i++)
        if (proc->fds[i].file != NULL)
            delete_arc_str(proc->fds[i].file);
}

static void clone_fds(process_t *parent, process_t *new) {
    memcpy(new->fds, parent->fds, sizeof(new->fds));
    int cnt = 0;
    for (int i = 0; i < MAX_MON_FDS; i++)
        if (new->fds[i].file != NULL) {
            new->fds[i].file->ref_cnt++;
            cnt++;
        }
    fprintf(stderr, "Cloned %d fds\n", cnt);
}

static void syscall_open(process_t *proc, int fd, int offset) {
    CHECK_FD;

#if 0
    dump_regs(&scall_regs, stderr);
#endif
    size_t remote_addr = proc->regs.regs.uregs[0 + offset];
    char *filename = copy_from_process_str(proc, remote_addr);
#if 0
    printf("open('%s')\n", filename);
#endif

    proc->fds[fd].file = new_arc_str(filename);
    proc->fds[fd].ioctl_exit = null_ioctl_exit_cb; // dump_ioctl_exit_cb;
    proc->fds[fd].read_exit = default_read_exit_cb;
    proc->fds[fd].write_exit = default_write_exit_cb;

    if (!strcmp(filename, "/dev/hi_i2c")) {
        proc->fds[fd].ioctl_enter = xm_i2c_ioctl_enter_cb;
        proc->fds[fd].ioctl_exit = xm_i2c_ioctl_exit_cb;
        show_i2c_banner(fd);
        return;
    }

    if (!strcmp(filename, "/dev/ssp")) {
        proc->fds[fd].ioctl_enter = ssp_ioctl_enter_cb;
        proc->fds[fd].ioctl_exit = ssp_ioctl_exit_cb;
        return;
    }

    if (!strcmp(filename, "/dev/xm_gpio")) {
        proc->fds[fd].ioctl_exit = xm_gpio_ioctl_exit_cb;
        return;
    }

    if (IS_PREFIX(filename, "/dev/i2c-")) {
        proc->fds[fd].write_exit = i2c_write_exit_cb;
        switch (chip_generation) {
        case HISI_V2:
        case HISI_V2A:
            proc->fds[fd].read_exit = hisi_gen2_read_exit_cb;
            break;
        case HISI_V3:
        case HISI_V3A:
        case HISI_V4:
        case HISI_V4A:
            proc->fds[fd].ioctl_enter = hisi_i2c_read_enter_cb;
            proc->fds[fd].ioctl_exit = hisi_i2c_read_exit_cb;
            break;
        default:
            proc->fds[fd].ioctl_exit =
                null_i2c_ioctl_exit_cb; // dump_i2c_ioctl_exit_cb;
        }
        show_i2c_banner(fd);
    } else if (IS_PREFIX(filename, "/dev/spidev2.0")) {
        proc->fds[fd].ioctl_exit = an41908a_ioctl_exit_cb;
    } else if (IS_PREFIX(filename, "/dev/spidev")) {
        proc->fds[fd].ioctl_exit = spi_ioctl_exit_cb;
    } else if (IS_PREFIX(filename, "/dev/mtd")) {
        proc->fds[fd].write_exit = mtd_write_cb;
        proc->fds[fd].ioctl_enter = mtd_ioctl_enter_cb;
        proc->fds[fd].ioctl_exit = mtd_ioctl_exit_cb;
    } else if (IS_PREFIX(filename, "/sys/class/gpio/gpio")) {
        proc->fds[fd].write_exit = gpio_write_cb;
    } else if (!strcmp(filename, "/dev/hi_mipi") ||
               !strcmp(filename, "/dev/mipi")) {
        proc->fds[fd].ioctl_exit = hisi_mipi_ioctl_exit_cb;
    }
}

static void syscall_close(process_t *proc, ssize_t sysret) {
    int fd = proc->regs.regs.uregs[0];
    CHECK_FD;

#if 0
    printf("close(%d)\n", fd);
#endif
    if (proc->fds[fd].file) {
        delete_arc_str(proc->fds[fd].file);
    }
    memset(&proc->fds[fd], 0, sizeof(proc->fds[fd]));
}

static void syscall_write_exit(process_t *proc, ssize_t sysret) {
    int fd = proc->regs.regs.uregs[0];
    CHECK_FD;

    size_t remote_addr = proc->regs.regs.uregs[1];
    size_t nbyte = proc->regs.regs.uregs[2];

    if (proc->fds[fd].write_exit)
        proc->fds[fd].write_exit(proc, fd, remote_addr, nbyte, sysret);
}

static void syscall_read_exit(process_t *proc, ssize_t sysret) {
    int fd = proc->regs.regs.uregs[0];
    CHECK_FD;

    size_t remote_addr = proc->regs.regs.uregs[1];
    size_t nbyte = proc->regs.regs.uregs[2];

    if (proc->fds[fd].read_exit)
        proc->fds[fd].read_exit(proc, fd, remote_addr, nbyte, sysret);
}

static void syscall_ioctl_exit(process_t *proc, ssize_t sysret) {
    int fd = proc->regs.regs.uregs[0];
    CHECK_FD;

    unsigned int cmd = proc->regs.regs.uregs[1];
    size_t arg = proc->regs.regs.uregs[2];

    if (proc->fds[fd].ioctl_exit)
        proc->fds[fd].ioctl_exit(proc, fd, cmd, arg, sysret);
}

static void syscall_nanosleep(process_t *proc, ssize_t sysret) {
    size_t remote_rqtp = proc->regs.regs.uregs[0];
    size_t remote_rmtp = proc->regs.regs.uregs[1];

    struct timespec req = {0};
    copy_from_process(proc->pid, remote_rqtp, &req, sizeof(req));
    printf("usleep(%ld)\n",
           (unsigned long)req.tv_sec * 1000000 + req.tv_nsec / 1000);
}

static bool usleep_disabled = false;

static void exit_syscall(process_t *proc) {
    int sysret = get_syscall_ret(proc);
    switch (proc->syscall_num) {
    case SYSCALL_OPEN:
        syscall_open(proc, sysret, 0);
        break;
    case SYSCALL_OPENAT:
        syscall_open(proc, sysret, 1);
        break;
    case SYSCALL_CLOSE:
        syscall_close(proc, sysret);
        break;
    case SYSCALL_READ:
        syscall_read_exit(proc, sysret);
        break;
    case SYSCALL_WRITE:
        syscall_write_exit(proc, sysret);
        break;
    case SYSCALL_IOCTL:
        syscall_ioctl_exit(proc, sysret);
        break;
    case SYSCALL_NANOSLEEP:
        if (!usleep_disabled)
            syscall_nanosleep(proc, sysret);
        break;
#if 0
    default:
        printf("syscall%d()\n", proc->syscall_num);
#endif
    }
}

static pid_t get_process_parent_id(const pid_t pid) {
    pid_t ppid = -1;
    char buffer[BUFSIZ];

    sprintf(buffer, "/proc/%d/stat", pid);
    FILE *fp = fopen(buffer, "r");
    if (fp) {
        size_t size = fread(buffer, sizeof(char), sizeof(buffer), fp);
        if (size > 0) {
            // See: http://man7.org/linux/man-pages/man5/proc.5.html section
            // /proc/[pid]/stat
            strtok(buffer, " ");              // (1) pid  %d
            strtok(NULL, " ");                // (2) comm  %s
            strtok(NULL, " ");                // (3) state  %c
            char *s_ppid = strtok(NULL, " "); // (4) ppid  %d
            ppid = atoi(s_ppid);
        }
        fclose(fp);
    }
    return ppid;
}

static void do_trace(pid_t tracee) {
    int status;

    ht_setup(&pids, sizeof(pid_t), sizeof(process_t), 10);
    pid_t tracer = getpid();

    printf("\n[%d] child %d created\n", tracer, tracee);
    ptrace(PTRACE_ATTACH, tracee, NULL,
           NULL); // child is the main thread
    process_t *mthread = &(process_t){.pid = tracee};
    mthread->fds[0].file = new_arc_str(strdup("stdin"));
    mthread->fds[1].file = new_arc_str(strdup("stdout"));
    mthread->fds[2].file = new_arc_str(strdup("stderr"));
    ht_insert(&pids, &tracee, mthread);

    wait(NULL);

    long ptraceOption = PTRACE_O_TRACECLONE;
    ptrace(PTRACE_SETOPTIONS, tracee, NULL, ptraceOption);
    ptrace(PTRACE_SYSCALL, tracee, 0, 0);

    while (1) {
        pid_t child_waited = waitpid(-1, &status, __WALL);

        if (child_waited == -1)
            break;

        if (WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP) {
            if (((status >> 16) & 0xffff) == PTRACE_EVENT_CLONE) {
                pid_t new_child;
                if (ptrace(PTRACE_GETEVENTMSG, child_waited, 0, &new_child) !=
                    -1) {
                    pid_t ppid = -1;
                    if (!ht_contains(&pids, &new_child)) {
                        ppid = get_process_parent_id(new_child);
                        // TODO: review
                        if (ppid == tracer)
                            ppid = tracee;
                        //
                        process_t *thread = &(process_t){.pid = new_child};
                        process_t *parent = ht_lookup(&pids, &ppid);
                        if (parent) {
                            clone_fds(parent, thread);
                            ht_insert(&pids, &new_child, thread);
                        } else {
                            fprintf(stderr, "Cannot find parent %d\n", ppid);
                        }
                    }
                    ptrace(PTRACE_SYSCALL, new_child, 0, 0);

                    printf("\nparent %d created child %d\n", ppid, new_child);
                }

                ptrace(PTRACE_SYSCALL, child_waited, 0, 0);
                continue;
            }
        }

        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            if (WIFEXITED(status))
                printf("\nchild %d exited with status %d\n", child_waited,
                       WEXITSTATUS(status));
            else
                printf("\nchild %d killed by signal %d\n", child_waited,
                       WTERMSIG(status));
            process_t *proc = ht_lookup(&pids, &child_waited);
            if (proc == NULL) {
                fprintf(stderr, "Cannot lookup PID %d\n", child_waited);
                break;
            }
            free_fds(proc);
            ht_erase(&pids, &child_waited);

            if (ht_is_empty(&pids))
                break;
        } else if (WIFSTOPPED(status)) {
            int stopCode = WSTOPSIG(status);
            if (stopCode == SIGTRAP) {
                process_t *proc = ht_lookup(&pids, &child_waited);
                if (proc == NULL) {
                    printf("BAD lookup for %d\n", child_waited);
                    break;
                }

                if (!proc->syscall_num) {
                    enter_syscall(proc);
                } else {
                    exit_syscall(proc);
                    proc->syscall_num = 0;
                }
            }
        }

        ptrace(PTRACE_SYSCALL, child_waited, 1, NULL);
    }
}

static void do_child(const char *program, char *const argv[]) {
    ptrace(PTRACE_TRACEME, 0, 0, 0);
    execv(program, argv);
    perror("execl");
}

static int help() {
    puts("Usage: ipctool trace [--skip=usleep] <full/path/to/executable> "
         "[program arguments]");
    return EXIT_FAILURE;
}

static void parse_skip(const char *option) {
    if (strstr(option, "usleep") != NULL) {
        usleep_disabled = true;
    }
}

int ptrace_cmd(int argc, char **argv) {
    if (argc < 2)
        return help();

    const struct option long_options[] = {
        {"skip", required_argument, NULL, 's'},
        {NULL, 0, NULL, 0},
    };
    int res;
    int option_index;

    while ((res = getopt_long_only(argc, argv, "", long_options,
                                   &option_index)) != -1) {
        switch (res) {
        case 's':
            parse_skip(optarg);
            break;
        case '?':
            return help();
        }
    }

    if (!getchipname()) {
        puts("Unknown chip");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();
    if (pid)
        do_trace(pid);
    else
        do_child(argv[optind], &argv[optind]);
    return EXIT_SUCCESS;
}
#endif
