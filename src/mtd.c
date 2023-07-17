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

#include "boards/xm.h"
#include "chipid.h"
#include "hal/common.h"
#include "hal/hisi/hal_hisi.h"
#include "mtd.h"
#include "sha1.h"
#include "tools.h"
#include "uboot.h"

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
    if (!regex_compile(&regex, "root=/dev/mtdblock([0-9]) rootfstype=(\\w+)"))
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
            char path[60], fs[30], attrs[80];
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

static bool examine_part(int part_num, size_t size, size_t erasesize,
                         uint32_t *sha1, cJSON **contains) {
    bool res = false;
    if (size > 0x1000000)
        return res;

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
                *contains = (*contains) ?: cJSON_CreateArray();
                cJSON *j_inner = cJSON_CreateObject();
                ADD_PARAM("name", "xmcrypto");
                ADD_PARAM_FMT("offset", "0x%x", off);
                cJSON_AddItemToArray(*contains, j_inner);

                break;
            }
            off -= 0x10000;
        }
    }

    if (!uenv_detected && part_num < 2) {
        int u_off = uboot_detect_env(addr, size, erasesize);
        if (u_off != -1) {
            uenv_detected = true;

            *contains = (*contains) ?: cJSON_CreateArray();
            cJSON *j_inner = cJSON_CreateObject();
            ADD_PARAM("name", "uboot-env");
            ADD_PARAM_FMT("offset", "0x%x", u_off);
            cJSON_AddItemToArray(*contains, j_inner);

            uboot_copyenv_int(addr + u_off);
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
    cJSON *json;
    cJSON *j_part;
    const char *mtd_type;
    ssize_t totalsz;
    mpoint_t mpoints[MAX_MPOINTS];
} enum_mtd_ctx;

static bool cb_mtd_info(int i, const char *name, struct mtd_info_user *mtd,
                        void *ctx) {
    enum_mtd_ctx *c = (enum_mtd_ctx *)ctx;
    cJSON *j_inner = c->json;

    if (!c->mtd_type) {
        if (mtd->type == MTD_NORFLASH)
            c->mtd_type = "nor";
        else if (mtd->type == MTD_NANDFLASH)
            c->mtd_type = "nand";
        ADD_PARAM("type", c->mtd_type);
        ADD_PARAM_FMT("block", "%dK", mtd->erasesize / 1024);
        if (strlen(nor_chip)) {
            ADD_PARAM("chip", nor_chip);
        }
        cJSON_AddItemToObject(j_inner, "partitions", c->j_part);
    }

    j_inner = cJSON_CreateObject();
    cJSON_AddItemToArray(c->j_part, j_inner);

    ADD_PARAM("name", name);
    ADD_PARAM_FMT("size", "0x%x", mtd->size);

    if (i < MAX_MPOINTS && *c->mpoints[i].path) {
        ADD_PARAM("path", c->mpoints[i].path);
    }
    if (!c->mpoints[i].rw) {
        cJSON *contains = NULL;
        uint32_t sha1 = 0;
        if (examine_part(i, mtd->size, mtd->erasesize, &sha1, &contains)) {
            ADD_PARAM_FMT("sha1", "%.8x", sha1);
            if (contains) {
                cJSON_AddItemToObject(j_inner, "contains", contains);
            }
        }
    }
    if (mtd->type == MTD_NORFLASH || mtd->type == MTD_NANDFLASH)
        c->totalsz += mtd->size;
    return true;
}

#define MAX_MTD 10

struct mtd_entry {
    int i;
    char name[80];
    struct mtd_info_user mtd;
    bool valid;
};

void enum_mtd_info(void *ctx, cb_mtd cb) {
    FILE *fp;
    char dev[80];
    int n = 0, es, ee;
    struct mtd_entry mtds[MAX_MTD] = {0};

    if ((fp = fopen("/proc/mtd", "r"))) {
        while (fgets(dev, sizeof dev, fp)) {
            if (sscanf(dev, "mtd%d: %x %x \"%64[^\"]\"", &mtds[n].i, &es, &ee,
                       mtds[n].name)) {
                snprintf(dev, sizeof dev, "/dev/mtd%d", mtds[n].i);
                int devfd = open(dev, O_RDWR);
                if (devfd < 0)
                    goto skip;

                if (ioctl(devfd, MEMGETINFO, &mtds[n].mtd) >= 0)
                    mtds[n].valid = true;

                close(devfd);
            skip:
                n++;
                if (n == MAX_MTD)
                    break;
            }
        }

        fclose(fp);
    }

    /*
     * Check if fix weird Anjoy partition order:
      0x000000000000-0x000000020000 : "BOOT"
      0x000000040000-0x0000001d0000 : "KERNEL"
      0x0000001d0000-0x0000007b0000 : "SYSTEM"
      0x000000020000-0x000000040000 : "UBOOT"
      0x0000007b0000-0x000000800000 : "DATA"
    */
    if (!strcmp("BOOT", mtds[0].name) && !strcmp("KERNEL", mtds[1].name) &&
        !strcmp("SYSTEM", mtds[2].name) && !strcmp("UBOOT", mtds[3].name) &&
        !strcmp("DATA", mtds[4].name)) {
        // kind of partitions sort to make them right order:
        // tmp <- (3) UBOOT
        // 3 <- (2) SYSTEM
        // 2 <- (1) KERNEL
        // 1 <- tmp
        struct mtd_entry tmp = mtds[3];
        mtds[3] = mtds[2];
        mtds[2] = mtds[1];
        mtds[1] = tmp;
    }

    for (int i = 0; i < n; i++) {
        if (mtds[i].valid && !cb(mtds[i].i, mtds[i].name, &mtds[i].mtd, ctx))
            break;
    }
}

cJSON *get_mtd_info() {
    enum_mtd_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.json = cJSON_CreateObject();
    ctx.j_part = cJSON_CreateArray();

    parse_partitions(ctx.mpoints);

    enum_mtd_info(&ctx, cb_mtd_info);
    if (!ctx.mtd_type) {
        // cb_mtd_info was never called.
        cJSON_Delete(ctx.j_part);
    }

    cJSON *j_inner = ctx.json;

    if (ctx.totalsz)
        ADD_PARAM_FMT("size", "%dM", ctx.totalsz / 1024 / 1024);

    if (hal_fmc_mode) {
        const char *fmc_mode = hal_fmc_mode();
        if (fmc_mode)
            ADD_PARAM("addr-mode", fmc_mode);
    }

    cJSON *json = cJSON_CreateArray();
    cJSON_AddItemToArray(json, ctx.json);
    return json;
}

static bool xm_warning;

int mtd_erase_block(int fd, int offset, int erasesize) {
    struct erase_info_user mtdEraseInfo;

    mtdEraseInfo.start = offset;
    mtdEraseInfo.length = erasesize;
    ioctl(fd, MEMUNLOCK, &mtdEraseInfo);

    if (ioctl(fd, MEMERASE, &mtdEraseInfo) < 0) {
        if (is_xm_board()) {
            if (!xm_warning)
                printf("Erase failed, trying XM specific algorithm...");
            if (!xm_flash_init(fd)) {
                fprintf(stderr, "xm_flash_init error\n");
                return -1;
            }
            if (!xm_spiflash_unlock_and_erase(fd, offset, erasesize)) {
                fprintf(stderr, "xm_spiflash_unlock_and_erase error\n");
                return -1;
            }
            if (!xm_warning) {
                printf("ok\n");
                xm_warning = true;
            }
            return 0;
        } else
            return -1;
    }

    return 0;
}

bool mtd_write_block(int fd, int offset, const char *data, size_t size) {
    // fprintf(stderr, "Seeking on mtd device to: %x\n", offset);
    lseek(fd, offset, SEEK_SET);

    // fprintf(stderr, "Writing buffer sized: %x\n", size);
    int nbytes = write(fd, data, size);
    if (nbytes != (int)size) {
        fprintf(stderr, "Writed block size is equal to %d rather than %d\n",
                nbytes, size);
        return false;
    }

    return true;
}

bool mtd_verify_block(int mtd, int fd, int offset, const char *data,
                      size_t size) {
    bool res = false;

    // fprintf(stderr, "Seeking on mtd device to: %x\n", offset);
    lseek(fd, offset, SEEK_SET);

    char *buf = malloc(size);

    // fprintf(stderr, "Reading buffer sized: %x\n", size);
    int nbytes = read(fd, buf, size);
    if (nbytes != (int)size) {
        fprintf(stderr, "Readed block size is equal to %d rather than %d\n",
                nbytes, size);
        goto quit;
    }

    if (memcmp(buf, data, size) != 0) {
        fprintf(
            stderr,
            "Block mtd%d [%#x, %#x] write verify error, possibly dead flash\n",
            mtd, offset, size);
        goto quit;
    }

    res = true;

quit:
    if (buf)
        free(buf);
    return res;
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

    bool res = false;
    if (mtd_erase_block(fd, offset, erasesize)) {
        fprintf(stderr, "Fail to erase +0x%x\n", offset);
        goto quit;
    }

    if (!mtd_write_block(fd, offset, data, size))
        goto quit;
    if (!mtd_verify_block(mtd, fd, offset, data, size))
        goto quit;

    res = true;

quit:
    close(fd);

    return res;
}

static void mtd_unlock(int fd, int offset, int erasesize) {
    struct erase_info_user mtdEraseInfo = {
        .start = offset,
        .length = erasesize,
    };

    int ret = ioctl(fd, MEMUNLOCK, &mtdEraseInfo);
    if (ret < 0)
        fprintf(stderr, "Error while mtd_unlock() = %d\n", ret);
}

static bool mtd_unlock_cb(int i, const char *name, struct mtd_info_user *mtd,
                          void *ctx) {
    int fd = mtd_open(i);
    if (fd < 0) {
        fprintf(stderr, "Could not open mtd device: %d\n", i);
        return false;
    }

    if (mtd->type == MTD_NORFLASH) {
        printf("%s\t%#x, %#x\n", name, 0, mtd->size);
        mtd_unlock(fd, 0, mtd->size);
    }

    close(fd);
    return true;
}

int mtd_unlock_cmd() {
    enum_mtd_info(NULL, mtd_unlock_cb);

    return EXIT_SUCCESS;
}
