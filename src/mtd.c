#include <regex.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <sys/ioctl.h>

#include "chipid.h"
#include "tools.h"

// TODO: refactor later
int yaml_printf(char *format, ...);

struct mtd_info_user {
    uint8_t type;
    uint32_t flags;
    uint32_t size; // Total size of the MTD
    uint32_t erasesize;
    uint32_t oobblock; // Size of OOB blocks (e.g. 512)
    uint32_t oobsize;  // Amount of OOB data per block (e.g. 16)
    uint32_t ecctype;
    uint32_t eccsize;
};

#ifndef MEMGETINFO
#define MEMGETINFO _IOR('M', 1, struct mtd_info_user)
#endif

#define MTD_NORFLASH 3
#define MTD_NANDFLASH 4

#define MAX_MPOINTS 10
#define MPOINT_LEN 90

static void get_rootfs(char mpoints[MAX_MPOINTS][MPOINT_LEN]) {
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
                snprintf(mpoints[i], MPOINT_LEN, "/,%s", line + start);
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

static void parse_partitions(char mpoints[MAX_MPOINTS][MPOINT_LEN]) {
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
                    snprintf(mpoints[n], MPOINT_LEN, "%s,%s", path, fs);
                    if (strstr(attrs, "rw"))
                        strcat(mpoints[n], ",rw");
                }
            }
        }
        fclose(fp);
    }
}

void print_mtd_info() {
    FILE *fp;

    char mpoints[MAX_MPOINTS][MPOINT_LEN] = {0};
    parse_partitions(mpoints);

    char dev[80], name[80];
    int i, es, ee, ret;
    struct mtd_info_user mtd;

    char partitions[4096];
    ssize_t partsz = 0;

    ssize_t totalsz = 0;
    const char *mtd_type = NULL;
    ssize_t erasesize = 0;
    if ((fp = fopen("/proc/mtd", "r"))) {
        while (fgets(dev, sizeof dev, fp)) {
            name[0] = 0;
            if (sscanf(dev, "mtd%d: %x %x \"%64[^\"]\"", &i, &es, &ee, name)) {
                snprintf(dev, sizeof dev, "/dev/mtd%d", i);
                int devfd = open(dev, O_RDWR);
                if (devfd < 0)
                    continue;

                if (ioctl(devfd, MEMGETINFO, &mtd) < 0) {
                    continue;
                }
                if (!mtd_type) {
                    if (mtd.type == MTD_NORFLASH)
                        mtd_type = "nor";
                    else if (mtd.type == MTD_NANDFLASH)
                        mtd_type = "nand";
                    erasesize = mtd.erasesize;
                }
                partsz +=
                    snprintf(partitions + partsz, sizeof partitions - partsz,
                             "      - name: %s\n"
                             "        size: 0x%x\n",
                             name, mtd.size);
                if (i < MAX_MPOINTS && *mpoints[i]) {
                    partsz += snprintf(partitions + partsz,
                                       sizeof partitions - partsz,
                                       "        path: %s\n", mpoints[i]);
                }
                totalsz += mtd.size;
            }
        }
        fclose(fp);
    }
    yaml_printf("rom:\n"
                "  - type: %s\n"
                "    size: %dM\n"
                "    block: %dK\n",
                mtd_type, totalsz / 1024 / 1024, erasesize / 1024);
    if (strlen(nor_chip)) {
        yaml_printf("    chip:\n%s", nor_chip);
    }
    if (partsz) {
        yaml_printf("    partitions:\n%s", partitions);
    }
}
