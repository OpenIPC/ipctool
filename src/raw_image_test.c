/*
 * raw_image_test: when `ipctool restore --raw` agrees to write. The checks are
 * all that stands between a wrong file and a camera, and restore itself cannot
 * run on a host.
 */
#include <stdio.h>
#include <string.h>

#include "raw_image.h"

static int failed;
#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);               \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, "\n");                                             \
            failed = 1;                                                        \
        }                                                                      \
    } while (0)

#define ES 0x10000u
#define FLASH 0x800000u

// 8 MB: boot, env, kernel, rootfs, as OpenIPC lays out a NOR
static void layout(raw_part_t *p) {
    static const uint64_t sizes[] = {0x40000, 0x10000, 0x200000, 0x5b0000};
    uint64_t off = 0;
    for (int i = 0; i < 4; i++) {
        p[i] = (raw_part_t){.num = i,
                            .size = sizes[i],
                            .erasesize = ES,
                            .nor = true,
                            .has_offset = true,
                            .offset = off};
        off += sizes[i];
    }
}

static void refused(const raw_part_t *p, size_t n, uint64_t size,
                    const char *expect) {
    char why[128] = "";
    CHECK(!raw_image_fits(p, n, size, why, sizeof why), "accepted, wanted '%s'",
          expect);
    CHECK(strstr(why, expect), "'%s' does not say '%s'", why, expect);
}

int main(void) {
    raw_part_t p[4];
    char why[128];

    layout(p);
    CHECK(raw_image_fits(p, 4, FLASH, why, sizeof why), "whole image: %s", why);

    // Old kernels have no sysfs offset; the layout still adds up
    for (int i = 0; i < 4; i++)
        p[i].has_offset = false;
    CHECK(raw_image_fits(p, 4, FLASH, why, sizeof why), "no offsets: %s", why);

    // The file is wrong. A zero-length file on a machine with no MTD at all
    // has to stop here too: the next step stops every process.
    layout(p);
    refused(p, 4, FLASH - 1, "the image is");
    refused(p, 4, FLASH + 1, "the image is");
    refused(p, 4, FLASH * 2, "the image is");
    refused(p, 4, 0, "the image is");
    refused(p, 0, 0, "no flash partitions");

    layout(p);
    p[2].nor = false;
    refused(p, 4, FLASH, "not NOR");

    // "all" at mtd0, or a partition enumeration could not open
    layout(p);
    for (int i = 0; i < 4; i++)
        p[i].num = i + 1;
    refused(p, 4, FLASH, "found mtd1 where mtd0");
    layout(p);
    p[2].num = 3;
    refused(p, 4, FLASH, "found mtd3 where mtd2");

    // Anjoy: UBOOT is mtd3 and lies second, whatever order it is listed in
    layout(p);
    p[1].num = 3;
    p[3].num = 1;
    refused(p, 4, FLASH, "found mtd3 where mtd1");

    // A hole in the middle of the flash that the sizes alone do not show
    layout(p);
    p[2].offset += ES;
    refused(p, 4, FLASH, "mtd2 starts at");

    layout(p);
    p[1].size += 1;
    refused(p, 4, FLASH, "erase blocks");
    layout(p);
    p[3].erasesize = 0x1000;
    refused(p, 4, FLASH, "erase blocks");
    layout(p);
    p[0].erasesize = 0;
    refused(p, 4, FLASH, "erase blocks");

    return failed;
}
