#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <fcntl.h>
#include <sys/ioctl.h>

#include "chipid.h"

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

void print_mtd_info() {
    FILE *fp;
    char dev[80];
    char name[80];
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
                totalsz += mtd.size;
            }
        }
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
