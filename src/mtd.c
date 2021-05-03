#include <netinet/in.h>
#include <regex.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <fcntl.h>
#include <linux/limits.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "chipid.h"
#include "hal_hisi.h"
#include "mtd.h"
#include "sha1.h"
#include "tools.h"
#include "uboot.h"
#include "vendors/xm.h"

// TODO: refactor later
int yaml_printf(char *format, ...);

#ifndef MEMGETINFO
#define MEMGETINFO _IOR('M', 1, struct mtd_info_user)
#endif

#define MTD_NORFLASH 3
#define MTD_NANDFLASH 4

#define MAX_MPOINTS 10
#define MPOINT_LEN 90

typedef struct {
    char path[MPOINT_LEN];
    bool rw;
} mpoint_t;

static void get_rootfs(mpoint_t mpoints[MAX_MPOINTS]) {
    FILE *f = fopen("/proc/cmdline", "r");
    if (!f)
        return;

    regex_t regex;
    regmatch_t matches[3];
    if (!compile_regex(&regex, "root=/dev/mtdblock([0-9]) rootfstype=(\\w+)"))
        goto exit;

    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    if ((read = getline(&line, &len, f)) != -1) {
        if (regexec(&regex, line, sizeof(matches) / sizeof(matches[0]),
                    (regmatch_t *)&matches, 0) == 0) {
            regoff_t start = matches[1].rm_so;
            regoff_t end = matches[1].rm_eo;
            line[end] = 0;
            int i = strtod(line + start, NULL);

            if (i < MAX_MPOINTS) {
                start = matches[2].rm_so;
                end = matches[2].rm_eo;
                line[end] = 0;
                snprintf(mpoints[i].path, MPOINT_LEN, "/,%s", line + start);
            }
        }
    }
    if (line)
        free(line);

exit:
    regfree(&regex);
    fclose(f);
    return;
}

static void parse_partitions(mpoint_t mpoints[MAX_MPOINTS]) {
    get_rootfs(mpoints);

    FILE *fp;
    if ((fp = fopen("/proc/mounts", "r"))) {
        char mount[80];
        while (fgets(mount, sizeof mount, fp)) {
            char path[80], fs[80], attrs[80];
            int n;

            if (sscanf(mount, "/dev/mtdblock%d %s %s %s", &n, path, fs,
                       attrs)) {
                if (n < MAX_MPOINTS) {
                    snprintf(mpoints[n].path, MPOINT_LEN, "%s,%s", path, fs);
                    if (strstr(attrs, "rw")) {
                        strcat(mpoints[n].path, ",rw");
                        mpoints[n].rw = true;
                    }
                }
            }
        }
        fclose(fp);
    }
}

char *open_mtdblock(int i, int *fd, uint32_t size, int flags) {
    char filename[PATH_MAX];

    snprintf(filename, sizeof filename, "/dev/mtdblock%d", i);
    *fd = open(filename, O_RDONLY);
    if (*fd == -1) {
        return NULL;
    }

    char *addr =
        (char *)mmap(NULL, size, PROT_READ, MAP_PRIVATE | flags, *fd, 0);
    if ((void *)addr == MAP_FAILED) {
        close(*fd);
        return NULL;
    }

    return addr;
}

static bool uenv_detected;

static bool examine_part(int part_num, size_t size, uint32_t *sha1,
                         char contains[1024]) {
    bool res = false;

    int fd;
    char *addr = open_mtdblock(
        part_num, &fd, size, MAP_POPULATE /* causes read-ahead on the file */);
    if (!addr)
        return res;

    if (part_num == 0 && is_xm_board()) {
        int off = size - 0x400 /* crypto size */;
        while (off > 0) {
            uint16_t magic = *(uint16_t *)(addr + off);
            if (magic == 0xD4D2) {
                sprintf(contains, "%010s- name: xmcrypto\n%012soffset: 0x%x\n",
                        "", "", off);
                break;
            }
            off -= 0x10000;
        }
    }

    if (!uenv_detected && part_num < 2) {
        size_t u_off = uboot_detect_env(addr, size);
        if (u_off != -1) {
            uenv_detected = true;
            sprintf(contains + strlen(contains),
                    "%010s- name: uboot-env\n%012soffset: 0x%x\n", "", "",
                    u_off);
            uboot_copyenv(addr + u_off);
        }
    }

    char digest[21] = {0};
    SHA1(digest, addr, size);
    *sha1 = ntohl(*(uint32_t *)&digest);

    res = true;
bailout:
    close(fd);
    return res;
}

typedef struct {
    const char *mtd_type;
    ssize_t totalsz;
    mpoint_t mpoints[MAX_MPOINTS];
} enum_mtd_ctx;

static bool cb_mtd_info(int i, const char *name, struct mtd_info_user *mtd,
                        void *ctx) {
    enum_mtd_ctx *c = (enum_mtd_ctx *)ctx;

    if (!c->mtd_type) {
        if (mtd->type == MTD_NORFLASH)
            c->mtd_type = "nor";
        else if (mtd->type == MTD_NANDFLASH)
            c->mtd_type = "nand";
        yaml_printf("rom:\n"
                    "  - type: %s\n"
                    "    block: %dK\n",
                    c->mtd_type, mtd->erasesize / 1024);
        if (strlen(nor_chip)) {
            yaml_printf("    chip:\n%s", nor_chip);
        }
        yaml_printf("    partitions:\n");
    }
    yaml_printf("      - name: %s\n"
                "        size: 0x%x\n",
                name, mtd->size);
    if (i < MAX_MPOINTS && *c->mpoints[i].path) {
        yaml_printf("        path: %s\n", c->mpoints[i].path);
    }
    if (!c->mpoints[i].rw) {
        char contains[1024] = {0};
        uint32_t sha1;
        examine_part(i, mtd->size, &sha1, contains);
        yaml_printf("        sha1: %.8x\n", sha1);
        if (*contains) {
            yaml_printf("        contains:\n%s", contains);
        }
    }
    c->totalsz += mtd->size;
    return true;
}

void enum_mtd_info(void *ctx, cb_mtd cb) {
    FILE *fp;
    char dev[80], name[80];
    int i, es, ee;
    struct mtd_info_user mtd;

    if ((fp = fopen("/proc/mtd", "r"))) {
        bool running = true;
        while (fgets(dev, sizeof dev, fp) && running) {
            name[0] = 0;
            if (sscanf(dev, "mtd%d: %x %x \"%64[^\"]\"", &i, &es, &ee, name)) {
                snprintf(dev, sizeof dev, "/dev/mtd%d", i);
                int devfd = open(dev, O_RDWR);
                if (devfd < 0)
                    continue;

                if (ioctl(devfd, MEMGETINFO, &mtd) >= 0) {
                    if (!cb(i, name, &mtd, ctx))
                        running = false;
                }
                close(devfd);
            }
        }
        fclose(fp);
    }
}

void print_mtd_info() {
    enum_mtd_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    parse_partitions(ctx.mpoints);
    enum_mtd_info(&ctx, cb_mtd_info);

    if (ctx.totalsz)
        yaml_printf("    size: %dM\n", ctx.totalsz / 1024 / 1024);
    if (!strcmp(VENDOR_HISI, chip_manufacturer))
        hisi_detect_fmc();
}

// static bool xm_inited;

int mtd_erase_block(int fd, int offset, int erasesize) {
    struct erase_info_user mtdEraseInfo;

    mtdEraseInfo.start = offset;
    mtdEraseInfo.length = erasesize;
    ioctl(fd, MEMUNLOCK, &mtdEraseInfo);

    if (ioctl(fd, MEMERASE, &mtdEraseInfo) < 0) {
        if (is_xm_board()) {
            printf("Erase failed, trying XM specific algorithm...\n");
            if (!xm_flash_init(fd)) {
                fprintf(stderr, "xm_flash_init error\n");
                return -1;
            }
            if (!xm_spiflash_unlock_and_erase(fd, offset, erasesize)) {
                fprintf(stderr, "xm_spiflash_unlock_and_erase error\n");
                return -1;
            }
            // xm_inited = true;
            return 0;
        } else
            return -1;
    }

    return 0;
}

int mtd_write_block(int fd, int offset, const char *data, size_t size) {
    // fprintf(stderr, "Seeking on mtd device to: %x\n", offset);
    lseek(fd, offset, SEEK_SET);

    // fprintf(stderr, "Writing buffer sized: %x\n", size);
    write(fd, data, size);

    return 0;
}

static int mtd_open(int mtd) {
    char dev[PATH_MAX];
    int ret;
    int flags = O_RDWR | O_SYNC;

    snprintf(dev, sizeof(dev), "/dev/mtd%d", mtd);
    return open(dev, flags);
}

bool mtd_write(int mtd, uint32_t offset, uint32_t erasesize, const char *data,
               size_t size) {
    int fd = mtd_open(mtd);
    if (fd < 0) {
        fprintf(stderr, "Could not open mtd device: %d\n", mtd);
        return false;
    }

    bool ret = true;
    if (mtd_erase_block(fd, offset, erasesize)) {
        fprintf(stderr, "Fail to erase +0x%x\n", offset);
        ret = false;
        goto bailout;
    }

    mtd_write_block(fd, offset, data, size);

bailout:
    close(fd);

    return ret;
}
