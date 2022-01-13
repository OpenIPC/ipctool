#include "ptrace.h"

#include <assert.h>
#include <errno.h>
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

#include "hal_xm.h"

#define SSP_READ_ALT 0x1
#define SSP_WRITE_ALT 0X3

#define LINE "=========================="

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
	if (WSTOPSIG(status) == 0) exit(0);
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
        buf[i / 4] = word;
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

static uint32_t syscall_num;
static struct user scall_regs;

enum MonFd {
    MONFD_NONE = 0,
    MONFD_I2C,
    MONFD_SPI,
    MONFD_MEM,
    MONFD_MTD,
    MONFD_WDT,
    MONFD_AMA
};

#define MAX_MON_FDS 1024
static u_int8_t mon_fds[MAX_MON_FDS];
static char *mon_filename[MAX_MON_FDS];

static void set_mon_fd(int fd, enum MonFd mfd) {
    if (fd >= 0 && fd < MAX_MON_FDS) {
        mon_fds[fd] = mfd;
    }
}

static void set_mon_filename(int fd, char *filename) {
    if (fd >= 0 && fd < MAX_MON_FDS)
        mon_filename[fd] = filename;
}

static bool check_mon_fd(int fd, enum MonFd mfd) {
    if (fd >= 0 && fd < MAX_MON_FDS) {
        return mon_fds[fd] == mfd;
    }
    return false;
}

static void xm_decode_i2c_read(pid_t child, uint32_t arg, size_t sysret) {
    I2C_DATA_S i2c_data = {0};

    void *ret = copy_from_process(child, arg, &i2c_data, sizeof(i2c_data));
    if (ret == NULL)
        return;

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

static void hisi_decode_i2c_write(int phase, pid_t child, uint32_t arg,
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

static void syscall_ioctl_enter(pid_t child) {
    int fd = scall_regs.regs.uregs[0];
    assert(fd >= 0 && fd < MAX_MON_FDS);

    unsigned int cmd = scall_regs.regs.uregs[1];
    size_t arg = scall_regs.regs.uregs[2];

    switch (mon_fds[fd]) {
    case MONFD_I2C:
        if (cmd == I2C_RDWR)
            hisi_decode_i2c_write(1, child, arg, 0);
        else if (cmd == CMD_I2C_WRITE)
            xm_decode_i2c_write(child, arg, 0);
        break;
    case MONFD_SPI:
        if (cmd == SSP_READ_ALT)
            ssp_decode_read(1, child, arg, 0);
        break;
    case MONFD_MTD:
        copy_from_process(child, arg, ioctl_arg, sizeof(ioctl_arg));
        break;
    default:;
    }
}

static void enter_syscall(pid_t child, int syscall_req) {
    memset(&scall_regs, 0, sizeof(scall_regs));
    ptrace(PTRACE_GETREGS, child, NULL, &scall_regs);
    // assert(errno == 0);
    syscall_num = scall_regs.regs.uregs[7];
    switch (syscall_num) {
    case SYSCALL_IOCTL: {
        syscall_ioctl_enter(child);
        break;
    }
    default:;
    }
}

static size_t get_syscall_ret(pid_t child) {
    struct user regs;
    memset(&regs, 0, sizeof(regs));
    ptrace(PTRACE_GETREGS, child, NULL, &regs);
    // assert(errno == 0);
    return regs.regs.uregs[0];
}

static const char i2c_prefix1[] = "/dev/i2c-";
static const char i2c_dev2[] = "/dev/hi_i2c";
static const char spi_prefix1[] = "/dev/spidev";
static const char spi_dev2[] = "/dev/ssp";
static const char mem_file[] = "/dev/mem";
static const char hi_mipi_file[] = "/dev/hi_mipi";
static const char mtd_prefix[] = "/dev/mtd";
static const char ama_prefix[] = "/dev/ttyAMA";
static const char wd_prefix[] = "/dev/watchdog";

static void syscall_open(pid_t child, size_t fd) {
#if 0
    dump_regs(&scall_regs, stderr);
#endif
    size_t remote_addr = scall_regs.regs.uregs[0];
    char *filename = copy_from_process_str(child, remote_addr);
#if 0
    printf("open('%s')\n", filename);
#endif

    set_mon_filename(fd, filename);
    if (!strncmp(filename, i2c_prefix1, sizeof i2c_prefix1 - 1) ||
        !strcmp(filename, i2c_dev2)) {
        set_mon_fd(fd, MONFD_I2C);
        static int last_i2c_fd;
        if (last_i2c_fd != fd) {
            printf("%s i2c-%d %s\n", LINE, fd, LINE);
            last_i2c_fd = fd;
        }
    } else if (!strncmp(filename, spi_prefix1, sizeof spi_prefix1 - 1) ||
               !strcmp(filename, spi_dev2)) {
        set_mon_fd(fd, MONFD_SPI);
    } else if (!strncmp(filename, mem_file, sizeof mem_file - 1)) {
        set_mon_fd(fd, MONFD_MEM);
    } else if (!strncmp(filename, mtd_prefix, sizeof mtd_prefix - 1)) {
        set_mon_fd(fd, MONFD_MTD);
    } else if (!strncmp(filename, ama_prefix, sizeof ama_prefix - 1)) {
        set_mon_fd(fd, MONFD_AMA);
    } else if (!strncmp(filename, wd_prefix, sizeof wd_prefix - 1)) {
        set_mon_fd(fd, MONFD_WDT);
    }
}

static void syscall_close(pid_t child, size_t sysret) {
    int fd = scall_regs.regs.uregs[0];
#if 0
    printf("close(%d)\n", fd);
#endif
    set_mon_fd(fd, MONFD_NONE);
    if (mon_filename[fd]) {
        free(mon_filename[fd]);
        mon_filename[fd] = NULL;
    }
}

static void syscall_write(pid_t child, size_t sysret) {
    int fd = scall_regs.regs.uregs[0];
    size_t remote_addr = scall_regs.regs.uregs[1];
    size_t count = scall_regs.regs.uregs[2];

    if (check_mon_fd(fd, MONFD_I2C)) {
        unsigned char *buf = alloca(count);
        void *res = copy_from_process(child, remote_addr, buf, count);
        if (!res) {
            printf("ERROR: write(%d, 0x%x, %d) -> read from addrspace\n", fd,
                   remote_addr, count);
            return;
        }
        u_int16_t addr = buf[0] << 8 | buf[1];
        u_int8_t val = buf[2];
        printf("sensor_write_register(0x%x, 0x%x);\n", addr, val);
    } else if (check_mon_fd(fd, MONFD_MTD)) {
        printf("mtd_write(%d, %zu, 0x%x)\n", fd, remote_addr, count);
    } else if (check_mon_fd(fd, MONFD_AMA)) {
        printf("write_AMA(0x%x)\n", count);
    }
}

static void hisi_gen2_decode_i2c_read(char *buf, size_t count) {
    // reg_width
    if (count == 2) {
        printf("i2c_read() = 0x%x\n", *(u_int16_t *)buf);
    } else {
        printf("i2c_read() = 0x%x\n", *(u_int8_t *)buf);
    }
}

static void syscall_read(pid_t child, size_t sysret) {
    int fd = scall_regs.regs.uregs[0];
    size_t remote_addr = scall_regs.regs.uregs[1];
    size_t count = scall_regs.regs.uregs[2];

    if (check_mon_fd(fd, MONFD_I2C)) {
        unsigned char *buf = alloca(count);
        copy_from_process(child, remote_addr, buf, count);
        hisi_gen2_decode_i2c_read(buf, count);
    } else if (check_mon_fd(fd, MONFD_AMA)) {
        printf("read(%d, ..., %d)\n", fd, count);
    } else {
#if 0
        printf("read(%d, ..., %d)\n", fd, count);
#endif
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

static void print_args(uint32_t *b, uint32_t *a, int cnt) {
    for (int i = 0; i < cnt; i++) {
        if (*b == *a)
            printf(", 0x%x", *(b + i));
        else
            printf(", 0x%x -> 0x%x", *(b + i), *(a + i));
    }
}

#define I2C_RDWR 0x0707 /* Combined R/W transfer (one STOP only) */
#define I2C_SLAVE_FORCE                                                        \
    0x0706 /* Use this slave address, even if it                               \
                               is already in use by a driver! */
static void syscall_ioctl_exit(pid_t child, size_t sysret) {
    int fd = scall_regs.regs.uregs[0];
    assert(fd >= 0 && fd < MAX_MON_FDS);

    unsigned int cmd = scall_regs.regs.uregs[1];
    size_t arg = scall_regs.regs.uregs[2];

    switch (mon_fds[fd]) {
    case MONFD_I2C:
        switch (cmd) {
        case I2C_SLAVE_FORCE:
            printf("sensor_i2c_change_addr(0x%x);\n", arg << 1);
            break;
        case I2C_RDWR:
            hisi_decode_i2c_write(2, child, arg, sysret);
            break;
        case CMD_I2C_READ:
            xm_decode_i2c_read(child, arg, sysret);
            break;
        default:
#if 0
            printf("ioctl_i2c('%s', 0x%x, 0x%x)\n", mon_filename[fd], cmd, arg);
#endif
            ;
        }
        break;

    case MONFD_SPI:
        switch (cmd) {
        case SSP_READ_ALT:
            ssp_decode_read(2, child, arg, sysret);
            return;
        case SSP_WRITE_ALT:
            ssp_decode_write(child, arg, sysret);
            return;
        default: {
            uint32_t d[2] = {0};
            copy_from_process(child, arg, &d, sizeof(d));
            if (*d > 0xb0000000) {
                uint32_t tx = 0;
                copy_from_process(child, *d, &tx, sizeof(tx));

                printf("ioctl_spi('%s', 0x%x, {%#.8x}\n", mon_filename[fd], cmd,
                       tx);
            } else {
                int num = 1;
                printf("ioctl_spi('%s', 0x%x, {", mon_filename[fd], cmd);
                print_args((uint32_t *)ioctl_arg, d, num);
                printf("}) = %d\n", sysret);
            }
        }
        }
        break;
    case MONFD_MTD: {
        uint32_t d[2] = {0};
        copy_from_process(child, arg, &d, sizeof(d));
        int num = 1;
        printf("mtd_ioctl('%s'(%d), %s (0x%x)", mon_filename[fd], fd,
               mtd_cmd_params(cmd, &num), cmd);
        print_args((uint32_t *)ioctl_arg, d, num);
        printf(") = %d\n", sysret);
    } break;
    default:
#if 0
        printf("ioctl('%s'(%d), 0x%x, 0x%x)\n", mon_filename[fd], fd, cmd,
               arg);
#endif
        ;
    }
}

static void syscall_nanosleep(pid_t child, size_t sysret) {
    size_t remote_rqtp = scall_regs.regs.uregs[0];
    size_t remote_rmtp = scall_regs.regs.uregs[1];

    struct timespec req = {0};
    copy_from_process(child, remote_rqtp, &req, sizeof(req));
    printf("usleep(%ld)\n",
           (unsigned long)req.tv_sec * 1000000 + req.tv_nsec / 1000);
}

static void exit_syscall(pid_t child) {
    int sysret = get_syscall_ret(child);
    switch (syscall_num) {
    case SYSCALL_OPEN:
        syscall_open(child, sysret);
        break;
    case SYSCALL_CLOSE:
        syscall_close(child, sysret);
        break;
    case SYSCALL_READ:
        syscall_read(child, sysret);
        break;
    case SYSCALL_WRITE:
        syscall_write(child, sysret);
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
        enter_syscall(child, syscall_req);

        // exit from syscall
        if (wait_for_syscall(child) != 0)
            break;
        exit_syscall(child);
    }
}

static void do_child(const char *program, char *const argv[]) {
    printf("[%d]: Child\n", getpid());

    mon_filename[0] = strdup("stdin");
    mon_filename[1] = strdup("stdout");
    mon_filename[2] = strdup("stderr");

    ptrace(PTRACE_TRACEME, 0, 0, 0);
    execv(program, argv);
    perror("execl");
}

static void do_cleanup() {
    for (int i = 0; i < MAX_MON_FDS; i++) {
        free(mon_filename[i]);
    }
}

int ptrace_cmd(int argc, char **argv) {
    if (argc < 2) {
        puts("Usage: ipctool ptrace <path/to/executable> [arguments]");
        return EXIT_FAILURE;
    }

    pid_t pid = fork();
    if (pid)
        do_trace(pid, 2);
    else
        do_child(argv[1], &argv[1]);
    return EXIT_SUCCESS;
}
