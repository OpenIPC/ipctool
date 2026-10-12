/*
 * ipctool nand-migrate: move a NAND camera off the retired layout with a
 * kernel volume of its own, onto the current one, with no UART.
 *
 * The retired layout (u-boot-xmedia e19b798a, OpenIPC firmware #2526): one
 * UBI device holding `kernel` (a FIT), `rootfs` (UBIFS) and `rootfs_data`,
 * booted by a bootcmd that reads the kernel volume. The current one (u-boot-
 * xmedia a138a692, firmware #2537): no kernel volume; the kernel is
 * /boot/fitImage inside the UBIFS rootfs, which U-Boot loads with ubifsload.
 * firmware's sysupgrade refuses the old one and asks for a reinstall from
 * U-Boot, which needs a serial console.
 *
 * Every U-Boot that saved the old layout's stock bootcmd can already boot the
 * new one (a FIT NAND build, which always has CMD_UBIFS), so no bootloader is
 * written. What changes is:
 *   1. the rootfs volume gets an image with the kernel in /boot,
 *   2. bootcmd becomes the current stock one,
 *   3. the kernel volume goes.
 * In that order, so a run stopped at any point after 1 still boots: the old
 * bootcmd boots the old kernel volume with the new rootfs, and the new
 * bootcmd boots /boot whether the kernel volume is there or not.
 *
 * Step 1 rewrites the volume the system runs from, which UBI refuses while
 * anything has it open. So the work is done by ipctool itself as PID 1, from
 * a RAM root: it pivots into a tmpfs and sends busybox init SIGQUIT, which
 * runs the shutdown actions, kills every process and execs /sbin/init -- the
 * ::restart: action firmware's inittab carries for sysupgrade. /sbin/init in
 * the RAM root is this binary, which is static.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/reboot.h>
#include <linux/watchdog.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "mtd.h"
#include "nand_layout.h"
#include "nand_migrate.h"
#include "sha1.h"

#ifndef UBI_IOCRMVOL
#define UBI_IOCRMVOL _IOW(UBI_IOC_MAGIC, 1, int32_t)
#endif

#define RAM_ROOT "/ram"
#define RAM_IMAGE "/rootfs.ubifs"
#define RAM_CONF "/nand-migrate.conf"
// What a run leaves in the camera's settings, so its outcome can be read
// over SSH afterwards as /root/nand-migrate.log: the overlay's upper
// directory is root/ in that volume.
#define LOG_PATH "/mnt/root/root/nand-migrate.log"

typedef struct {
    int ubi, rootfs, kernel, data; // UBI device and volume ids
    int env_mtd;
    size_t image_size;
    char image[128]; // where phase 2 finds it, in the RAM root
    bool wipe;
    char stop[16]; // test knob: "write" or "env" stops after that step
} nm_conf_t;

/* ---- what is on the camera ---------------------------------------------- */

static int mtd_by_name(const char *want) {
    FILE *f = fopen("/proc/mtd", "r");
    if (!f)
        return -1;
    char line[128], name[64];
    int num, found = -1;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "mtd%d: %*x %*x \"%63[^\"]\"", &num, name) == 2 &&
            !strcmp(name, want)) {
            found = num;
            break;
        }
    fclose(f);
    return found;
}

static long sysfs_long(int ubi, int vol, const char *attr) {
    char path[96];
    snprintf(path, sizeof path, "/sys/class/ubi/ubi%d_%d/%s", ubi, vol, attr);
    FILE *f = fopen(path, "r");
    long v = -1;
    if (f) {
        if (fscanf(f, "%ld", &v) != 1)
            v = -1;
        fclose(f);
    }
    return v;
}

static char *read_file(const char *path, size_t *len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;
    size_t cap = 4096, n = 0;
    char *b = malloc(cap + 1);
    for (ssize_t r; b && (r = read(fd, b + n, cap - n)) > 0;) {
        n += r;
        if (n == cap) {
            char *nb = realloc(b, (cap *= 2) + 1);
            if (!nb)
                free(b);
            b = nb;
        }
    }
    close(fd);
    if (b)
        b[n] = '\0';
    if (len)
        *len = n;
    return b;
}

// The image, mapped rather than read: on a tmpfs it is RAM already, and a
// copy on the heap is RAM a 128 MB camera does not have twice.
static uint8_t *map_file(const char *path, size_t *size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;
    struct stat st;
    void *m = MAP_FAILED;
    if (!fstat(fd, &st) && st.st_size > 0)
        m = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (m == MAP_FAILED)
        return NULL;
    *size = st.st_size;
    return m;
}

// The whole env partition, or the first erase block of it -- where U-Boot
// keeps the env. NULL and errno when it cannot be read.
static uint8_t *read_env(int mtd, size_t *len, uint32_t *erasesize) {
    char dev[32];
    snprintf(dev, sizeof dev, "/dev/mtd%d", mtd);
    int fd = open(dev, O_RDONLY);
    if (fd < 0)
        return NULL;
    struct mtd_info_user info;
    uint8_t *b = NULL;
    if (ioctl(fd, MEMGETINFO, &info))
        goto out;
    *erasesize = info.erasesize;
    *len = info.size;
    b = malloc(info.size);
    if (b && pread(fd, b, info.size, 0) != (ssize_t)info.size) {
        free(b);
        b = NULL;
    }
out:
    close(fd);
    return b;
}

static bool is_static_binary(const char *path) {
    size_t len;
    char *elf = read_file(path, &len);
    if (!elf || len < 52 || memcmp(elf, "\177ELF", 4)) {
        free(elf);
        return false;
    }
    bool is64 = elf[4] == 2, dyn = false;
    uint64_t phoff = is64 ? *(uint64_t *)(elf + 32) : *(uint32_t *)(elf + 28);
    uint16_t phentsize = *(uint16_t *)(elf + (is64 ? 54 : 42));
    uint16_t phnum = *(uint16_t *)(elf + (is64 ? 56 : 44));
    for (int i = 0; i < phnum; i++) {
        size_t o = phoff + (size_t)i * phentsize;
        if (o + 4 <= len && *(uint32_t *)(elf + o) == 3 /* PT_INTERP */)
            dyn = true;
    }
    free(elf);
    return !dyn;
}

static long mem_available_kb(void) {
    char *m = read_file("/proc/meminfo", NULL);
    char *p = m ? strstr(m, "MemAvailable:") : NULL;
    long kb = p ? atol(p + 13) : -1;
    free(m);
    return kb;
}

/* ---- the flash steps, shared by both phases ------------------------------ */

static int wdt_fd = -1;

static void feed(void) {
    if (wdt_fd < 0)
        return;
    ioctl(wdt_fd, WDIOC_KEEPALIVE, 0);
    ssize_t unused = write(wdt_fd, "k", 1);
    (void)unused;
}

static char logbuf[16384];
static size_t loglen;

static void say(const char *fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((size_t)n >= sizeof line)
        n = sizeof line - 1;
    printf("nand-migrate: %s\n", line);
    fflush(stdout);
    if (loglen + n + 1 < sizeof logbuf) {
        memcpy(logbuf + loglen, line, n);
        logbuf[loglen + n] = '\n';
        loglen += n + 1;
    }
}

static bool ubi_rmvol(int ubi, int vol) {
    char dev[32];
    snprintf(dev, sizeof dev, "/dev/ubi%d", ubi);
    int fd = open(dev, O_RDONLY);
    if (fd < 0)
        return false;
    int32_t id = vol;
    bool ok = !ioctl(fd, UBI_IOCRMVOL, &id);
    int e = errno;
    close(fd);
    errno = e;
    return ok;
}

// ubiupdatevol: UBI takes the volume exclusively for the update, and EBUSY
// means something still has it -- a lazily detached UBIFS on its way out.
static bool ubi_volup(int ubi, int vol, const uint8_t *data, size_t size) {
    char dev[32];
    snprintf(dev, sizeof dev, "/dev/ubi%d_%d", ubi, vol);
    int fd = -1;
    int64_t bytes = size;
    for (int i = 0; i < 100; i++) {
        feed();
        fd = open(dev, O_RDWR);
        if (fd >= 0 && !ioctl(fd, UBI_IOCVOLUP, &bytes))
            break;
        int e = errno;
        if (fd >= 0)
            close(fd);
        fd = -1;
        if (e != EBUSY) {
            errno = e;
            return false;
        }
        usleep(100 * 1000);
    }
    if (fd < 0) {
        errno = EBUSY;
        return false;
    }
    size_t done = 0, chunk = 128 * 1024, step = size / 10 + 1, next = step;
    while (done < size) {
        size_t n = size - done < chunk ? size - done : chunk;
        ssize_t w = write(fd, data + done, n);
        if (w <= 0) {
            int e = errno;
            close(fd);
            errno = e;
            return false;
        }
        done += w;
        feed();
        if (done >= next || done == size) {
            say("  written %zu%%", done * 100 / size);
            next += step;
        }
    }
    return !close(fd);
}

static bool sha1_matches(int ubi, int vol, const uint8_t *data, size_t size) {
    unsigned char want[20], got[20];
    SHA1_CTX ctx;
    SHA1Init(&ctx);
    for (size_t o = 0; o < size; o += 65536) {
        size_t n = size - o < 65536 ? size - o : 65536;
        SHA1Update(&ctx, data + o, n);
    }
    SHA1Final(want, &ctx);
    size_t read_len = 0;
    feed();
    return sha1_ubi_volume(ubi, vol, size, got, &read_len) &&
           read_len == size && !memcmp(want, got, sizeof want);
}

/*
 * Write env back to the start of its partition, every erase block it spans
 * erased first, and read it back. NAND has no partial-block erase, and
 * U-Boot's env_nand writes the env from the partition's first good block,
 * which the caller has checked is block 0.
 */
static bool write_env(int mtd, const uint8_t *env, size_t len,
                      uint32_t erasesize) {
    char dev[32];
    snprintf(dev, sizeof dev, "/dev/mtd%d", mtd);
    int fd = open(dev, O_RDWR);
    if (fd < 0)
        return false;
    bool ok = false;
    uint8_t *back = malloc(len);
    for (uint32_t o = 0; o < len; o += erasesize) {
        struct erase_info_user e = {.start = o, .length = erasesize};
        feed();
        if (ioctl(fd, MEMERASE, &e))
            goto out;
    }
    if (pwrite(fd, env, len, 0) != (ssize_t)len)
        goto out;
    ok = back && pread(fd, back, len, 0) == (ssize_t)len &&
         !memcmp(env, back, len);
out:
    free(back);
    close(fd);
    return ok;
}

/* ---- PID 1 --------------------------------------------------------------- */

static void keep_log(const nm_conf_t *c) {
    // Into the settings volume, where the next boot shows it as
    // /root/nand-migrate.log. Not after --wipe-settings: an empty volume is
    // what makes init start from defaults.
    if (c->wipe)
        return;
    char src[32];
    snprintf(src, sizeof src, "ubi%d:rootfs_data", c->ubi);
    mkdir("/mnt", 0755);
    if (mount(src, "/mnt", "ubifs", 0, NULL))
        return;
    mkdir("/mnt/root", 0755);
    mkdir("/mnt/root/root", 0700);
    int fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        ssize_t unused = write(fd, logbuf, loglen);
        (void)unused;
        fsync(fd);
        close(fd);
    }
    umount("/mnt");
}

static void __attribute__((noreturn)) finish(const nm_conf_t *c, bool ok) {
    say(ok ? "done, rebooting" : "rebooting");
    if (c)
        keep_log(c);
    sync();
    sleep(1);
    reboot(RB_AUTOBOOT);
    for (;;)
        pause();
}

// Detach the old root and everything mounted under it, deepest first.
static void release_old_root(void) {
    char *mounts = read_file("/proc/mounts", NULL);
    char *paths[64];
    int n = 0;
    for (char *l = mounts ? strtok(mounts, "\n") : NULL; l && n < 64;
         l = strtok(NULL, "\n")) {
        char dev[128], path[128];
        if (sscanf(l, "%127s %127s", dev, path) == 2 &&
            (!strcmp(path, "/mnt") || !strncmp(path, "/mnt/", 5)))
            paths[n++] = strdup(path);
    }
    // Longest path first: a mount under /mnt goes before /mnt
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (strlen(paths[j]) > strlen(paths[i])) {
                char *t = paths[i];
                paths[i] = paths[j];
                paths[j] = t;
            }
    for (int i = 0; i < n; i++) {
        if (umount2(paths[i], MNT_DETACH))
            say("cannot detach %s: %s", paths[i], strerror(errno));
        free(paths[i]);
    }
    free(mounts);
}

static bool read_conf(nm_conf_t *c) {
    char *s = read_file(RAM_CONF, NULL);
    if (!s)
        return false;
    int wipe = 0;
    int n = sscanf(s,
                   "ubi=%d rootfs=%d kernel=%d data=%d env_mtd=%d "
                   "image_size=%zu image=%127s wipe=%d stop=%15s",
                   &c->ubi, &c->rootfs, &c->kernel, &c->data, &c->env_mtd,
                   &c->image_size, c->image, &wipe, c->stop);
    free(s);
    c->wipe = wipe;
    if (!strcmp(c->stop, "-"))
        c->stop[0] = '\0';
    return n == 9;
}

int nand_migrate_pid1(void) {
    int con = open("/dev/console", O_RDWR | O_NOCTTY);
    if (con >= 0) {
        dup2(con, 0);
        dup2(con, 1);
        dup2(con, 2);
        if (con > 2)
            close(con);
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    // The watchdog's old feeder is dead; its timer runs from its last ping
    wdt_fd = open("/dev/watchdog", O_WRONLY);
    if (wdt_fd >= 0) {
        int t = 60;
        ioctl(wdt_fd, WDIOC_SETTIMEOUT, &t);
        feed();
    }
    say("running as PID 1 from RAM");

    nm_conf_t c;
    if (!read_conf(&c)) {
        say("cannot read " RAM_CONF ", nothing was written");
        finish(NULL, false);
    }
    release_old_root();

    size_t size = 0;
    uint8_t *img = map_file(c.image, &size);
    if (!img || size != c.image_size) {
        say("the image in RAM is not what was checked, nothing was written");
        finish(&c, false);
    }

    /*
     * 1. the rootfs, with the kernel in /boot. This is the one step a
     * failure is not safe in: once UBI has begun the update, the old rootfs
     * is gone, and the kernel volume kept for step 2 boots nothing without
     * it. A write-then-swap needs the image's size free in the UBI device,
     * and this layout has none: rootfs_data was created with autoresize.
     * So a failure is retried from the copy in RAM -- a new update starts
     * the volume over -- and only a run that cannot write it at all reboots,
     * into a camera that needs its serial console.
     */
    say("writing the rootfs volume ubi%d_%d (%zu bytes)", c.ubi, c.rootfs,
        size);
    bool written = false;
    for (int attempt = 1; attempt <= 3 && !written; attempt++) {
        if (attempt > 1)
            say("retrying the rootfs write (attempt %d of 3)", attempt);
        if (!ubi_volup(c.ubi, c.rootfs, img, size))
            say("writing the rootfs failed: %s", strerror(errno));
        else if (!sha1_matches(c.ubi, c.rootfs, img, size))
            say("the rootfs reads back different from the image");
        else
            written = true;
    }
    if (!written) {
        say("the rootfs volume could not be written and is incomplete; the "
            "camera will not boot until it is reinstalled over its serial "
            "console");
        finish(&c, false);
    }
    say("rootfs written and read back: SHA-1 matches");
    munmap(img, size);
    if (!strcmp(c.stop, "write"))
        finish(&c, true);

    // 2. bootcmd. The old kernel volume stays until this is on flash: until
    // then the old bootcmd boots it, with the new rootfs.
    size_t plen, elen;
    uint32_t erasesize;
    uint8_t *part = read_env(c.env_mtd, &plen, &erasesize);
    elen = part ? nm_env_locate(part, plen) : 0;
    const char *cmd = elen ? nm_env_get(part, elen, "bootcmd") : NULL;
    if (!cmd ||
        (strcmp(cmd, NM_BOOTCMD_FITVOL) && strcmp(cmd, NM_BOOTCMD_UBIFS))) {
        say("the U-Boot env changed under the run; it boots the old kernel "
            "volume with the new rootfs");
        finish(&c, false);
    }
    if (strcmp(cmd, NM_BOOTCMD_UBIFS)) {
        if (!nm_env_set(part, elen, "bootcmd", NM_BOOTCMD_UBIFS)) {
            say("no room in the env for the new bootcmd; it boots the old "
                "kernel volume with the new rootfs");
            finish(&c, false);
        }
        bool ok = write_env(c.env_mtd, part, elen, erasesize);
        // Read it back from the flash as U-Boot will
        free(part);
        part = read_env(c.env_mtd, &plen, &erasesize);
        cmd = part && nm_env_locate(part, plen) == elen
                  ? nm_env_get(part, elen, "bootcmd")
                  : NULL;
        if (!ok || !cmd || strcmp(cmd, NM_BOOTCMD_UBIFS)) {
            // An env that fails its CRC gets U-Boot's built-in one, whose
            // bootcmd reads the kernel volume: keep the volume, it boots.
            say("writing the env failed; the kernel volume is kept, which "
                "boots");
            finish(&c, false);
        }
    }
    free(part);
    say("bootcmd now loads the kernel from /boot in the rootfs");
    if (!strcmp(c.stop, "env"))
        finish(&c, true);

    // 3. the kernel volume
    if (!ubi_rmvol(c.ubi, c.kernel))
        say("removing the kernel volume failed (%s); it is unused now, and "
            "`ipctool nand-migrate` removes it",
            strerror(errno));
    else
        say("kernel volume removed");

    if (c.wipe) {
        if (ubi_volup(c.ubi, c.data, NULL, 0))
            say("settings wiped");
        else
            say("wiping the settings failed: %s", strerror(errno));
    }
    finish(&c, true);
}

/* ---- the command --------------------------------------------------------- */

static void unwind(void) {
    mount(RAM_ROOT "/tmp", "/tmp", NULL, MS_MOVE, NULL);
    mount(RAM_ROOT "/sys", "/sys", NULL, MS_MOVE, NULL);
    mount(RAM_ROOT "/proc", "/proc", NULL, MS_MOVE, NULL);
    mount(RAM_ROOT "/dev", "/dev", NULL, MS_MOVE, NULL);
    umount2(RAM_ROOT, MNT_DETACH);
}

static bool write_whole(const char *path, const void *data, size_t len,
                        mode_t mode) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0)
        return false;
    bool ok = write(fd, data, len) == (ssize_t)len;
    return !close(fd) && ok;
}

// Whether path is a file on the /tmp tmpfs, which the hand-off carries
// into the RAM root whole -- so the image needs no second copy in RAM.
static bool on_tmp_tmpfs(const char *path) {
    char *mounts = read_file("/proc/mounts", NULL);
    bool tmpfs = mounts && strstr(mounts, " /tmp tmpfs ");
    free(mounts);
    // Not under a mount of its own below /tmp, which would not move with it
    struct stat f, t;
    return tmpfs && !strncmp(path, "/tmp/", 5) && !stat(path, &f) &&
           !stat("/tmp", &t) && f.st_dev == t.st_dev;
}

static int hand_off(nm_conf_t *c, const char *path, const uint8_t *img,
                    size_t size) {
    char opts[64];
    bool move_tmp = on_tmp_tmpfs(path);
    // This binary and a few files, plus the image when it has to be copied
    snprintf(opts, sizeof opts, "size=%zuk",
             (move_tmp ? 0 : size / 1024) + 4096);
    mkdir(RAM_ROOT, 0755);
    if (mount("tmpfs", RAM_ROOT, "tmpfs", 0, opts)) {
        fprintf(stderr, "Cannot mount a RAM root on %s: %s\n", RAM_ROOT,
                strerror(errno));
        return 1;
    }
    if (move_tmp)
        snprintf(c->image, sizeof c->image, "%s", path);
    else
        snprintf(c->image, sizeof c->image, "%s", RAM_IMAGE);
    size_t elen;
    char *self = read_file("/proc/self/exe", &elen);
    char conf[384];
    snprintf(conf, sizeof conf,
             "ubi=%d rootfs=%d kernel=%d data=%d env_mtd=%d image_size=%zu "
             "image=%s wipe=%d stop=%s\n",
             c->ubi, c->rootfs, c->kernel, c->data, c->env_mtd, size, c->image,
             c->wipe, *c->stop ? c->stop : "-");
    bool ok = self && !mkdir(RAM_ROOT "/sbin", 0755) &&
              !mkdir(RAM_ROOT "/dev", 0755) && !mkdir(RAM_ROOT "/proc", 0755) &&
              !mkdir(RAM_ROOT "/sys", 0755) && !mkdir(RAM_ROOT "/tmp", 01777) &&
              !mkdir(RAM_ROOT "/mnt", 0755) &&
              write_whole(RAM_ROOT "/sbin/init", self, elen, 0755) &&
              (move_tmp || write_whole(RAM_ROOT RAM_IMAGE, img, size, 0644)) &&
              write_whole(RAM_ROOT RAM_CONF, conf, strlen(conf), 0644);
    free(self);
    if (!ok) {
        fprintf(stderr, "Cannot stage the RAM root: %s\n", strerror(errno));
        umount2(RAM_ROOT, MNT_DETACH);
        return 1;
    }

    printf("\nHanding PID 1 over to the RAM root. SSH ends here; the camera\n"
           "writes the flash on its own and reboots when done (about a\n"
           "minute). Progress goes to the serial console, and the log to\n"
           "/root/nand-migrate.log once it is back.\n");
    fflush(stdout);
    sync();

    // Moved, not bound: nothing may point back into the old root
    if (mount("/dev", RAM_ROOT "/dev", NULL, MS_MOVE, NULL) ||
        mount("/proc", RAM_ROOT "/proc", NULL, MS_MOVE, NULL) ||
        (move_tmp && mount("/tmp", RAM_ROOT "/tmp", NULL, MS_MOVE, NULL))) {
        fprintf(stderr, "Cannot move /dev, /proc and /tmp: %s\n",
                strerror(errno));
        unwind();
        return 1;
    }
    mount("/sys", RAM_ROOT "/sys", NULL, MS_MOVE, NULL);
    if (chdir(RAM_ROOT) || syscall(SYS_pivot_root, ".", "mnt")) {
        fprintf(stderr, "Cannot pivot into the RAM root: %s\n",
                strerror(errno));
        (void)!chdir("/");
        unwind();
        return 1;
    }
    // From here PID 1's root is the RAM root too; it execs our /sbin/init
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    kill(1, SIGQUIT);
    sleep(30);
    // Still here: init did not hand over. Nothing has been written.
    reboot(RB_AUTOBOOT);
    return 1;
}

static void usage(void) {
    puts("Usage: ipctool nand-migrate [--dry-run] [--wipe-settings] "
         "<rootfs.ubifs>\n"
         "\n"
         "Moves a NAND camera off the retired layout with a `kernel` UBI\n"
         "volume onto the current one, where the kernel is /boot/fitImage in\n"
         "the UBIFS rootfs: writes <rootfs.ubifs> (from the -nand- package\n"
         "of the image to install) into the rootfs volume, sets the current\n"
         "stock bootcmd, removes the kernel volume and reboots. U-Boot is\n"
         "not touched. Settings are kept unless --wipe-settings.");
}

int nand_migrate_cmd(int argc, char **argv) {
    const struct option opts[] = {
        {"dry-run", no_argument, NULL, 'n'},
        {"wipe-settings", no_argument, NULL, 'w'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };
    nm_conf_t c = {0};
    bool dry = false;
    int o;
    while ((o = getopt_long(argc, argv, "nwh", opts, NULL)) != -1) {
        switch (o) {
        case 'n':
            dry = true;
            break;
        case 'w':
            c.wipe = true;
            break;
        default:
            usage();
            return o == 'h' ? 0 : 1;
        }
    }
    const char *stop = getenv("IPCTOOL_MIGRATE_STOP");
    if (stop)
        snprintf(c.stop, sizeof c.stop, "%s", stop);

    // What the camera is
    int mtd_ubi = mtd_by_name("ubi");
    c.env_mtd = mtd_by_name("env");
    c.ubi = mtd_ubi >= 0 ? find_ubi_for_mtd(mtd_ubi) : -1;
    if (c.ubi < 0 || c.env_mtd < 0) {
        fprintf(stderr, "Not a NAND camera with `env` and `ubi` partitions "
                        "and UBI attached; nothing to migrate.\n");
        return 1;
    }
    ubi_vol_info_t vols[MAX_UBI_VOLS];
    int nvols = enum_ubi_volumes(c.ubi, vols, MAX_UBI_VOLS);
    c.rootfs = c.kernel = c.data = -1;
    for (int i = 0; i < nvols; i++) {
        if (!strcmp(vols[i].name, "rootfs"))
            c.rootfs = vols[i].vol_id;
        else if (!strcmp(vols[i].name, "kernel"))
            c.kernel = vols[i].vol_id;
        else if (!strcmp(vols[i].name, "rootfs_data"))
            c.data = vols[i].vol_id;
    }
    uint8_t head[4] = {0};
    size_t head_len = 0;
    if (c.kernel >= 0) {
        char dev[32];
        snprintf(dev, sizeof dev, "/dev/ubi%d_%d", c.ubi, c.kernel);
        int fd = open(dev, O_RDONLY);
        if (fd >= 0) {
            ssize_t n = read(fd, head, sizeof head);
            head_len = n > 0 ? n : 0;
            close(fd);
        }
    }
    size_t plen, elen = 0;
    uint32_t erasesize = 0;
    uint8_t *env = read_env(c.env_mtd, &plen, &erasesize);
    if (env)
        elen = nm_env_locate(env, plen);
    if (!elen) {
        fprintf(stderr,
                "Cannot read a U-Boot env from mtd%d; nothing was "
                "written.\n",
                c.env_mtd);
        free(env);
        return 1;
    }
    char *cmdline = read_file("/proc/cmdline", NULL);
    nm_camera_t cam = {
        .has_kernel_vol = c.kernel >= 0,
        .has_rootfs_vol = c.rootfs >= 0,
        .has_data_vol = c.data >= 0,
        .cmdline = cmdline,
        .kernel_head = head,
        .kernel_head_len = head_len,
        .bootcmd = nm_env_get(env, elen, "bootcmd"),
        .bootargs = nm_env_get(env, elen, "bootargs"),
    };
    const char *why;
    nm_plan_t plan = nm_check_camera(&cam, &why);
    free(cmdline);
    free(env);
    if (plan == NM_REFUSE) {
        fprintf(stderr,
                "Cannot migrate this camera: %s. Nothing was "
                "written.\n",
                why);
        return 1;
    }

    if (plan == NM_DROP_KERNEL_VOL) {
        // It booted with the new bootcmd, so /boot is on the running rootfs;
        // confirm before taking away what the old one would boot.
        struct stat st;
        if (stat("/boot/fitImage", &st) && stat("/boot/uImage", &st)) {
            fprintf(stderr, "bootcmd boots /boot, but the running rootfs has "
                            "no kernel there; the kernel volume is kept.\n");
            return 1;
        }
        printf("bootcmd already boots /boot/%s; only the unused kernel volume "
               "ubi%d_%d is left.\n",
               access("/boot/fitImage", F_OK) ? "uImage" : "fitImage", c.ubi,
               c.kernel);
        if (dry)
            return 0;
        if (!ubi_rmvol(c.ubi, c.kernel)) {
            fprintf(stderr, "Removing it failed: %s\n", strerror(errno));
            return 1;
        }
        printf("Removed. This camera is on the current NAND layout; "
               "sysupgrade upgrades it from now on.\n");
        return 0;
    }

    // The image
    if (optind >= argc) {
        usage();
        return 1;
    }
    // Absolute and resolved: whether it is on /tmp decides how it is staged
    char *resolved = realpath(argv[optind], NULL);
    if (!resolved) {
        fprintf(stderr, "Cannot read %s: %s\n", argv[optind], strerror(errno));
        return 1;
    }
    // Phase 2 is told where the image is through a one-line conf
    char path[sizeof c.image];
    bool fits = strlen(resolved) < sizeof path && !strpbrk(resolved, " \t\n");
    if (fits)
        strcpy(path, resolved);
    else
        fprintf(stderr,
                "Cannot use %s: put the image at a shorter path with "
                "no spaces, such as /tmp/rootfs.ubifs. Nothing was "
                "written.\n",
                resolved);
    free(resolved);
    if (!fits)
        return 1;
    size_t size;
    uint8_t *img = map_file(path, &size);
    if (!img) {
        fprintf(stderr, "Cannot read %s: %s\n", path, strerror(errno));
        return 1;
    }
    long leb = sysfs_long(c.ubi, c.rootfs, "usable_eb_size");
    long ebs = sysfs_long(c.ubi, c.rootfs, "reserved_ebs");
    const char *kname;
    why = nm_check_image(img, size, leb, &kname);
    if (!why && leb > 0 && ebs > 0 && (long long)size > (long long)leb * ebs)
        why = "it is bigger than the rootfs volume";
    if (why) {
        fprintf(stderr, "Cannot install %s: %s. Nothing was written.\n", path,
                why);
        munmap(img, size);
        return 1;
    }
    // An image on the /tmp tmpfs moves into the RAM root as it is; any
    // other is copied there, which takes its size in RAM
    long avail = mem_available_kb();
    long need = (on_tmp_tmpfs(path) ? 0 : (long)(size / 1024)) + 8192;
    if (avail >= 0 && avail < need) {
        fprintf(stderr,
                "Not enough free RAM to stage %s (%ld KB available). "
                "Nothing was written.\n",
                path, avail);
        munmap(img, size);
        return 1;
    }
    if (!is_static_binary("/proc/self/exe")) {
        fprintf(stderr, "This ipctool is dynamically linked and cannot run "
                        "from an empty RAM root; use a release build. Nothing "
                        "was written.\n");
        munmap(img, size);
        return 1;
    }
    struct stat st;
    if (access("/etc/inittab", F_OK) == 0) {
        char *it = read_file("/etc/inittab", NULL);
        bool restart = it && strstr(it, "::restart:/sbin/init");
        free(it);
        if (!restart) {
            fprintf(stderr, "/etc/inittab has no ::restart:/sbin/init, so init "
                            "cannot be handed the flash phase. Nothing was "
                            "written.\n");
            munmap(img, size);
            return 1;
        }
    }
    {
        char dev[32];
        snprintf(dev, sizeof dev, "/dev/mtd%d", c.env_mtd);
        int fd = open(dev, O_RDONLY);
        long long zero = 0; // MEMGETBADBLOCK takes a loff_t
        int bad = fd >= 0 ? ioctl(fd, MEMGETBADBLOCK, &zero) : -1;
        if (fd >= 0)
            close(fd);
        if (bad > 0) {
            fprintf(stderr, "The env's first block is bad; not writing the "
                            "env. Nothing was written.\n");
            munmap(img, size);
            return 1;
        }
    }
    (void)st;

    printf("Camera: retired NAND layout, UBI device ubi%d on mtd%d\n"
           "  kernel ubi%d_%d (FIT), rootfs ubi%d_%d (%ld KB), "
           "rootfs_data ubi%d_%d\n"
           "  bootcmd: the kernel-volume stock one; env at mtd%d, %zu bytes\n"
           "Image: %s, %zu KB, UBIFS for %ld-byte LEBs, kernel %s\n"
           "Plan, from a RAM root as PID 1:\n"
           "  1. write the image into ubi%d_%d and read it back\n"
           "  2. set bootcmd to the current stock one (ubifsload /boot/...)\n"
           "  3. remove the kernel volume ubi%d_%d\n"
           "  4. %s, then reboot\n",
           c.ubi, mtd_ubi, c.ubi, c.kernel, c.ubi, c.rootfs, leb * ebs / 1024,
           c.ubi, c.data, c.env_mtd, elen, path, size / 1024, leb, kname, c.ubi,
           c.rootfs, c.ubi, c.kernel,
           c.wipe ? "wipe the settings" : "keep the settings");
    if (dry) {
        printf("Dry run: nothing was written.\n");
        munmap(img, size);
        return 0;
    }
    int ret = hand_off(&c, path, img, size);
    munmap(img, size);
    return ret;
}
