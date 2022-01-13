#include "ptrace.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
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
#include "hal_hisi.h"
#include "hal_xm.h"

#define IS_PREFIX(name, substr) (!strncmp(name, substr, sizeof substr - 1))

#define SSP_READ_ALT 0x1
#define SSP_WRITE_ALT 0X3

#define LINE "=========================="

typedef void (*read_enter_hook)(pid_t child, int fd, size_t buf, size_t nbyte);
typedef void (*read_exit_hook)(pid_t child, int fd, size_t buf, size_t nbyte,
                               size_t sysret);
typedef void (*write_enter_hook)(pid_t child, int fd, size_t buf, size_t nbyte);
typedef void (*write_exit_hook)(pid_t child, int fd, size_t buf, size_t nbyte,
                                size_t sysret);
typedef void (*ioctl_enter_hook)(pid_t child, int fd, unsigned int cmd,
                                 size_t arg);
typedef void (*ioctl_exit_hook)(pid_t child, int fd, unsigned int cmd,
                                size_t arg, size_t sysret);

static int wait_for_syscall(pid_t child) {
    int status;
    while (1) {
        ptrace(PTRACE_SYSCALL, child, 0, 0);
        waitpid(child, &status, 0);
        if (WIFSTOPPED(status) && WSTOPSIG(status) & 0x80)
            return 0;
        if (WIFEXITED(status))
            return 1;
        fprintf(stderr, "[stopped pid %d status %d (%x)], exiting\n", child,
                status, WSTOPSIG(status));
        if (WSTOPSIG(status) == 0)
            exit(0);
    }
}

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

static void *copy_from_process(pid_t child, size_t addr, void *ptr,
                               size_t size) {
    size_t *buf = ptr;
    for (size_t i = 0; i < size; i += sizeof(size_t)) {
        size_t word = ptrace(PTRACE_PEEKTEXT, child, addr + i, 0);
        if (word == -1 && errno) {
            printf("error copy_from_process from %#x\n", addr);
            return NULL;
        }
        buf[i / sizeof(size_t)] = word;
    }
    return ptr;
}

static char *copy_from_process_str(pid_t child, size_t addr) {
    size_t word;
    ssize_t buflen = 1024;
    char *buf = malloc(buflen);
    size_t readlen = 0;

    do {
        if (buflen == readlen) {
            buflen *= 2;
            buf = realloc(buf, buflen);
            assert(buf);
        }
        word = ptrace(PTRACE_PEEKTEXT, child, addr + readlen, 0);
        assert(errno == 0);
        *(size_t *)(buf + readlen) = word;
        readlen += sizeof(size_t);
    } while (!memchr(&word, 0, sizeof(word)));
    return buf;
}

static struct user scall_regs;

typedef struct {
    char *filename;

    read_enter_hook read_enter;
    read_exit_hook read_exit;

    write_enter_hook write_enter;
    write_exit_hook write_exit;

    ioctl_enter_hook ioctl_enter;
    ioctl_exit_hook ioctl_exit;
} mon_fd_t;

#define MAX_MON_FDS 1024
static mon_fd_t fds[MAX_MON_FDS];

static void xm_i2c_change_addr(int new_addr) {
    static int old_addr;
    if (old_addr != new_addr) {
        printf("sensor_i2c_change_addr(%#x)\n", new_addr);
        old_addr = new_addr;
    }
}

static void xm_decode_i2c_read(pid_t child, uint32_t arg, size_t sysret) {
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

static void xm_decode_i2c_write(pid_t child, uint32_t arg, size_t sysret) {
    I2C_DATA_S i2c_data = {0};

    void *ret = copy_from_process(child, arg, &i2c_data, sizeof(i2c_data));
    if (ret == NULL)
        return;

    xm_i2c_change_addr(i2c_data.dev_addr);
    printf("sensor_write_register(%#x, %#x);\n", i2c_data.reg_addr,
           i2c_data.data);
}

static void ssp_decode_read(int phase, pid_t child, uint32_t arg,
                            size_t sysret) {
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

static void ssp_decode_write(pid_t child, uint32_t arg, size_t sysret) {
    uint32_t value;

    void *ret = copy_from_process(child, arg, &value, sizeof(value));
    if (ret == NULL)
        return;

    printf("ssp_write_register(%#x, %#x);\n", value >> 8, value & 0xff);
}

static void hisi_decode_sns_read(int phase, pid_t child, uint32_t arg,
                                 size_t sysret) {
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

static void hisi_i2c_read_enter_cb(pid_t child, int fd, unsigned int cmd,
                                   size_t arg) {
    if (cmd == I2C_RDWR)
        hisi_decode_sns_read(1, child, arg, 0);
}

static void xm_i2c_ioctl_enter_cb(pid_t child, int fd, unsigned int cmd,
                                  size_t arg) {
    if (cmd == CMD_I2C_WRITE)
        xm_decode_i2c_write(child, arg, 0);
}

static void ssp_ioctl_enter_cb(pid_t child, int fd, unsigned int cmd,
                               size_t arg) {
    if (cmd == SSP_READ_ALT)
        ssp_decode_read(1, child, arg, 0);
}

static void mtd_ioctl_enter_cb(pid_t child, int fd, unsigned int cmd,
                               size_t arg) {
    copy_from_process(child, arg, ioctl_arg, sizeof(ioctl_arg));
}

static void syscall_ioctl_enter(pid_t child) {
    int fd = scall_regs.regs.uregs[0];
    assert(fd >= 0 && fd < MAX_MON_FDS);

    unsigned int cmd = scall_regs.regs.uregs[1];
    size_t arg = scall_regs.regs.uregs[2];

    if (fds[fd].ioctl_enter)
        fds[fd].ioctl_enter(child, fd, cmd, arg);
}

static size_t enter_syscall(pid_t child, int syscall_req) {
    memset(&scall_regs, 0, sizeof(scall_regs));
    ptrace(PTRACE_GETREGS, child, NULL, &scall_regs);
    assert(errno == 0);

    size_t syscall_num = scall_regs.regs.uregs[7];
    switch (syscall_num) {
    case SYSCALL_IOCTL: {
        syscall_ioctl_enter(child);
        break;
    }
    default:;
    }

    return syscall_num;
}

static size_t get_syscall_ret(pid_t child) {
    struct user regs;
    memset(&regs, 0, sizeof(regs));
    ptrace(PTRACE_GETREGS, child, NULL, &regs);
    // assert(errno == 0);
    return regs.regs.uregs[0];
}

static void default_i2c_ioctl_exit_cb(pid_t child, int fd, unsigned int cmd,
                                      size_t arg, size_t sysret) {
#if 0
    printf("ioctl_i2c('%s', 0x%x, 0x%x)\n", fds[fd].filename, cmd, arg);
#endif
}

static void default_ioctl_exit_cb(pid_t child, int fd, unsigned int cmd,
                                  size_t arg, size_t sysret) {
#if 0
    printf("ioctl('%s'(%d), 0x%x, 0x%x)\n", fds[fd].filename, fd, cmd, arg);
#endif
}

static void xm_i2c_ioctl_exit_cb(pid_t child, int fd, unsigned int cmd,
                                 size_t arg, size_t sysret) {
    switch (cmd) {
    case I2C_RDWR:
        hisi_decode_sns_read(2, child, arg, sysret);
        break;
    case CMD_I2C_READ:
        xm_decode_i2c_read(child, arg, sysret);
        break;
    }
}

static void hisi_i2c_read_exit_cb(pid_t child, int fd, unsigned int cmd,
                                  size_t arg, size_t sysret) {
    switch (cmd) {
    case I2C_SLAVE_FORCE:
        printf("sensor_i2c_change_addr(0x%x);\n", arg << 1);
        break;
    case I2C_RDWR:
        hisi_decode_sns_read(2, child, arg, sysret);
        break;
    }
}

static void ssp_ioctl_exit_cb(pid_t child, int fd, unsigned int cmd, size_t arg,
                              size_t sysret) {
    switch (cmd) {
    case SSP_READ_ALT:
        ssp_decode_read(2, child, arg, sysret);
        break;
    case SSP_WRITE_ALT:
        ssp_decode_write(child, arg, sysret);
        break;
    }
}

static void dump_hisi_read_mipi(pid_t child, size_t remote_addr) {
    size_t stsize = hisi_sizeof_combo_dev_attr();
    char *buf = alloca(stsize);
    if (!copy_from_process(child, remote_addr, buf, stsize))
        return;

    hisi_dump_combo_dev_attr(buf);
}

static void hisi_mipi_ioctl_exit_cb(pid_t child, int fd, unsigned int cmd,
                                    size_t arg, size_t sysret) {
    switch (cmd) {
    case HI_MIPI_RESET_MIPI:
        break;
    case HI_MIPI_RESET_SENSOR:
        break;
    case HI_MIPI_UNRESET_MIPI:
        break;
    case HI_MIPI_UNRESET_SENSOR:
        break;
    case HI_MIPI_SET_DEV_ATTR:
        dump_hisi_read_mipi(child, arg);
        break;
    default:
        printf("ERR: uknown cmd %#x for himipi\n", arg);
    }
}

static void hisi_gen2_read_exit_cb(pid_t child, int fd, size_t remote_addr,
                                   size_t nbyte, size_t sysret) {
    unsigned char *buf = alloca(nbyte);
    copy_from_process(child, remote_addr, buf, nbyte);
    // reg_width
    if (nbyte == 2) {
        printf("i2c_read() = 0x%x\n", *(u_int16_t *)buf);
    } else {
        printf("i2c_read() = 0x%x\n", *(u_int8_t *)buf);
    }
}

static void default_read_exit_cb(pid_t child, int fd, size_t remote_addr,
                                 size_t nbyte, size_t sysret) {
#if 0
    printf("read(%d, ..., %d)\n", fd, nbyte);
#endif
}

static void default_write_exit_cb(pid_t child, int fd, size_t remote_addr,
                                  size_t nbyte, size_t sysret) {
#if 0
    printf("write(%d, ..., %d)\n", fd, nbyte);
#endif
}

static void i2c_write_exit_cb(pid_t child, int fd, size_t remote_addr,
                              size_t nbyte, size_t sysret) {
    unsigned char *buf = alloca(nbyte);
    void *res = copy_from_process(child, remote_addr, buf, nbyte);
    if (!res) {
        printf("ERROR: write(%d, 0x%x, %d) -> read from addrspace\n", fd,
               remote_addr, nbyte);
        return;
    }
    u_int16_t addr = buf[0] << 8 | buf[1];
    u_int8_t val = buf[2];
    printf("sensor_write_register(0x%x, 0x%x);\n", addr, val);
}

static void mtd_write_cb(pid_t child, int fd, size_t remote_addr, size_t nbyte,
                         size_t sysret) {
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

static void spi_ioctl_exit_cb(pid_t child, int fd, unsigned int cmd, size_t arg,
                              size_t sysret) {
    uint32_t d[2] = {0};
    copy_from_process(child, arg, &d, sizeof(d));
    if (*d > 0xb0000000) {
        uint32_t tx = 0;
        copy_from_process(child, *d, &tx, sizeof(tx));

        printf("ioctl_spi('%s', 0x%x, {%#.8x}\n", fds[fd].filename, cmd, tx);
    } else {
        int num = 1;
        printf("ioctl_spi('%s', 0x%x, {", fds[fd].filename, cmd);
        print_args((uint32_t *)ioctl_arg, d, num);
        printf("}) = %d\n", sysret);
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

static void mtd_ioctl_exit_cb(pid_t child, int fd, unsigned int cmd, size_t arg,
                              size_t sysret) {
    uint32_t d[2] = {0};
    copy_from_process(child, arg, &d, sizeof(d));
    int num = 1;
    printf("mtd_ioctl('%s'(%d), %s (0x%x)", fds[fd].filename, fd,
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

static void syscall_open(pid_t child, size_t fd) {
    assert(fd >= 0 && fd < MAX_MON_FDS);

#if 0
    dump_regs(&scall_regs, stderr);
#endif
    size_t remote_addr = scall_regs.regs.uregs[0];
    char *filename = copy_from_process_str(child, remote_addr);
#if 0
    printf("open('%s')\n", filename);
#endif

    fds[fd].filename = filename;
    fds[fd].ioctl_exit = default_ioctl_exit_cb;
    fds[fd].read_exit = default_read_exit_cb;
    fds[fd].write_exit = default_write_exit_cb;

    if (!strcmp(filename, "/dev/hi_i2c")) {
        fds[fd].ioctl_enter = xm_i2c_ioctl_enter_cb;
        fds[fd].ioctl_exit = xm_i2c_ioctl_exit_cb;
        show_i2c_banner(fd);
        return;
    }

    if (!strcmp(filename, "/dev/ssp")) {
        fds[fd].ioctl_enter = ssp_ioctl_enter_cb;
        fds[fd].ioctl_exit = ssp_ioctl_exit_cb;
        return;
    }

    if (IS_PREFIX(filename, "/dev/i2c-")) {
        fds[fd].write_exit = i2c_write_exit_cb;
        switch (chip_generation) {
        case HISI_V2:
        case HISI_V2A:
            fds[fd].read_exit = hisi_gen2_read_exit_cb;
            break;
        case HISI_V3:
        case HISI_V4:
        case HISI_V4A:
            fds[fd].ioctl_enter = hisi_i2c_read_enter_cb;
            fds[fd].ioctl_exit = hisi_i2c_read_exit_cb;
            break;
        default:
            fds[fd].ioctl_exit = default_i2c_ioctl_exit_cb;
        }
        show_i2c_banner(fd);
    } else if (IS_PREFIX(filename, "/dev/spidev")) {
        fds[fd].ioctl_exit = spi_ioctl_exit_cb;
    } else if (IS_PREFIX(filename, "/dev/mtd")) {
        fds[fd].write_exit = mtd_write_cb;
        fds[fd].ioctl_enter = mtd_ioctl_enter_cb;
        fds[fd].ioctl_exit = mtd_ioctl_exit_cb;
    } else if (IS_PREFIX(filename, "/dev/ttyAMA")) {
        // TODO
    } else if (!strcmp(filename, "/dev/hi_mipi") ||
               !strcmp(filename, "/dev/mipi")) {
        switch (chip_generation) {
        case HISI_V4:
        case HISI_V4A:
            fds[fd].ioctl_exit = hisi_mipi_ioctl_exit_cb;
            break;
        }
    }
}

static void syscall_close(pid_t child, size_t sysret) {
    int fd = scall_regs.regs.uregs[0];
    assert(fd >= 0 && fd < MAX_MON_FDS);

#if 0
    printf("close(%d)\n", fd);
#endif
    if (fds[fd].filename) {
        free(fds[fd].filename);
    }
    memset(&fds[fd], 0, sizeof(fds[fd]));
}

static void syscall_write_exit(pid_t child, size_t sysret) {
    int fd = scall_regs.regs.uregs[0];
    assert(fd >= 0 && fd < MAX_MON_FDS);

    size_t remote_addr = scall_regs.regs.uregs[1];
    size_t nbyte = scall_regs.regs.uregs[2];

    if (fds[fd].write_exit)
        fds[fd].write_exit(child, fd, remote_addr, nbyte, sysret);
}

static void syscall_read_exit(pid_t child, size_t sysret) {
    int fd = scall_regs.regs.uregs[0];
    assert(fd >= 0 && fd < MAX_MON_FDS);

    size_t remote_addr = scall_regs.regs.uregs[1];
    size_t nbyte = scall_regs.regs.uregs[2];

    if (fds[fd].read_exit)
        fds[fd].read_exit(child, fd, remote_addr, nbyte, sysret);
}

static void syscall_ioctl_exit(pid_t child, size_t sysret) {
    int fd = scall_regs.regs.uregs[0];
    assert(fd >= 0 && fd < MAX_MON_FDS);

    unsigned int cmd = scall_regs.regs.uregs[1];
    size_t arg = scall_regs.regs.uregs[2];

    if (fds[fd].ioctl_exit)
        fds[fd].ioctl_exit(child, fd, cmd, arg, sysret);
}

static void syscall_nanosleep(pid_t child, size_t sysret) {
    size_t remote_rqtp = scall_regs.regs.uregs[0];
    size_t remote_rmtp = scall_regs.regs.uregs[1];

    struct timespec req = {0};
    copy_from_process(child, remote_rqtp, &req, sizeof(req));
    printf("usleep(%ld)\n",
           (unsigned long)req.tv_sec * 1000000 + req.tv_nsec / 1000);
}

static void exit_syscall(pid_t child, size_t syscall_num) {
    int sysret = get_syscall_ret(child);
    switch (syscall_num) {
    case SYSCALL_OPEN:
        syscall_open(child, sysret);
        break;
    case SYSCALL_CLOSE:
        syscall_close(child, sysret);
        break;
    case SYSCALL_READ:
        syscall_read_exit(child, sysret);
        break;
    case SYSCALL_WRITE:
        syscall_write_exit(child, sysret);
        break;
    case SYSCALL_IOCTL:
        syscall_ioctl_exit(child, sysret);
        break;
    case SYSCALL_NANOSLEEP:
        syscall_nanosleep(child, sysret);
        break;
#if 0
    default:
        printf("syscall%d()\n", syscall_num);
#endif
    }
}

static void do_trace(pid_t child, int syscall_req) {
    printf("[%d]: Parent for %d\n", getpid(), child);
    int status;
    waitpid(child, &status, 0);
    assert(WIFSTOPPED(status));
    ptrace(PTRACE_SETOPTIONS, child, 0, PTRACE_O_TRACESYSGOOD);

    while (1) {
        // enter syscall
        if (wait_for_syscall(child) != 0)
            break;
        size_t syscall_num = enter_syscall(child, syscall_req);

        // exit from syscall
        if (wait_for_syscall(child) != 0)
            break;
        exit_syscall(child, syscall_num);
    }
}

static void do_child(const char *program, char *const argv[]) {
    printf("[%d]: Child\n", getpid());

    fds[0].filename = strdup("stdin");
    fds[1].filename = strdup("stdout");
    fds[2].filename = strdup("stderr");

    ptrace(PTRACE_TRACEME, 0, 0, 0);
    execv(program, argv);
    perror("execl");
}

int ptrace_cmd(int argc, char **argv) {
    if (argc < 2) {
        puts("Usage: ipctool ptrace <full/path/to/executable> [arguments]");
        return EXIT_FAILURE;
    }

    if (!getchipid()) {
        puts("Unknown chip");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();
    if (pid)
        do_trace(pid, 2);
    else
        do_child(argv[1], &argv[1]);
    return EXIT_SUCCESS;
}
