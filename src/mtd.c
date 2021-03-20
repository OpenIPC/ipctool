#include <netinet/in.h>
#include <regex.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "chipid.h"
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

static bool uenv_detected;

static bool examine_part(int part_num, size_t size, uint32_t *sha1,
                         char contains[1024]) {
    char filename[1024];
    bool res = false;

    snprintf(filename, sizeof filename, "/dev/mtdblock%d", part_num);
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        return false;
    }

    if (!size) {
        struct stat buf;
        fstat(fd, &buf);
        size = buf.st_size;
    }

    char *addr = (char *)mmap(
        NULL, size, PROT_READ,
        MAP_PRIVATE | MAP_POPULATE /* causes read-ahead on the file */, fd, 0);
    if ((void *)addr == MAP_FAILED) {
        res = false;
        goto bailout;
    }

    if (part_num == 0 && is_xm_board()) {
        size_t off = size - 0x400 /* crypto size */;
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

static void cb_mtd_info(int i, const char *name, struct mtd_info_user *mtd,
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
}

void enum_mtd_info(void *ctx, cb_mtd cb) {
    FILE *fp;
    char dev[80], name[80];
    int i, es, ee;
    struct mtd_info_user mtd;

    if ((fp = fopen("/proc/mtd", "r"))) {
        while (fgets(dev, sizeof dev, fp)) {
            name[0] = 0;
            if (sscanf(dev, "mtd%d: %x %x \"%64[^\"]\"", &i, &es, &ee, name)) {
                snprintf(dev, sizeof dev, "/dev/mtd%d", i);
                int devfd = open(dev, O_RDWR);
                if (devfd < 0)
                    continue;

                if (ioctl(devfd, MEMGETINFO, &mtd) >= 0)
                    cb(i, name, &mtd, ctx);
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

    yaml_printf("    size: %dM\n", ctx.totalsz / 1024 / 1024);
}
