/*
 * raw_image_test: when `ipctool restore --raw` agrees to write, and to which
 * partitions. The checks are all that stands between a wrong file and a
 * camera, and restore itself cannot run on a host.
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
#define MAX 8

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
    raw_part_t plan[MAX];
    CHECK(!raw_image_plan(p, n, size, plan, why, sizeof why),
          "accepted, wanted '%s'", expect);
    CHECK(strstr(why, expect), "'%s' does not say '%s'", why, expect);
}

// The plan is these devices, in this order
static void planned(const raw_part_t *p, size_t n, const int *nums, size_t m) {
    char why[128] = "";
    raw_part_t plan[MAX];
    size_t got = raw_image_plan(p, n, FLASH, plan, why, sizeof why);
    CHECK(got == m, "%zu partitions planned, wanted %zu: %s", got, m, why);
    for (size_t k = 0; k < got && k < m; k++)
        CHECK(plan[k].num == nums[k], "plan[%zu] is mtd%d", k, plan[k].num);
}

int main(void) {
    raw_part_t p[MAX];

    layout(p);
    planned(p, 4, (int[]){0, 1, 2, 3}, 4);

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

    // Nothing could say where this one is; its size proves nothing
    layout(p);
    p[2].has_offset = false;
    refused(p, 4, FLASH, "cannot tell where mtd2");

    // A partition that could not be opened
    layout(p);
    p[2] = p[3];
    refused(p, 3, FLASH, "no partition covers 0x50000-0x250000");
    layout(p);
    refused(p, 3, FLASH, "the image is");

    // Numbers need not follow the flash: "all" went first and was left out
    // by enumeration, so the real ones start at mtd1
    layout(p);
    for (int i = 0; i < 4; i++)
        p[i].num = i + 1;
    planned(p, 4, (int[]){1, 2, 3, 4}, 4);

    // Anjoy: UBOOT is mtd3 and lies second, whichever order it is listed in
    layout(p);
    p[1].num = 3;
    p[3].num = 1;
    planned(p, 4, (int[]){0, 3, 2, 1}, 4);
    raw_part_t swapped = p[1];
    p[1] = p[3];
    p[3] = swapped;
    planned(p, 4, (int[]){0, 3, 2, 1}, 4);

    // A second name for one partition, and one inside what is already picked
    layout(p);
    p[4] = p[2];
    p[4].num = 4;
    planned(p, 5, (int[]){0, 1, 2, 3}, 4);
    p[4].offset = p[2].offset + ES;
    p[4].size = ES;
    planned(p, 5, (int[]){0, 1, 2, 3}, 4);

    // hi3516cv610 on a kernel with mtdsplit: what is left once firmware has
    // gone as an alias. rootfs_data is the tail of rootfs and is not written
    // twice.
    layout(p);
    p[3].size = 0x5b0000;
    p[4] = p[3];
    p[4].num = 4;
    p[4].offset = 0x6d0000;
    p[4].size = 0x130000;
    planned(p, 5, (int[]){0, 1, 2, 3}, 4);

    // A parent still standing is written through, in either listing order
    layout(p);
    p[2] = (raw_part_t){.num = 2,
                        .size = 0x7b0000,
                        .erasesize = ES,
                        .nor = true,
                        .has_offset = true,
                        .offset = 0x50000};
    p[3] = p[2];
    p[3].num = 3;
    p[3].size = 0x200000;
    planned(p, 4, (int[]){0, 1, 2}, 3);
    raw_part_t parent = p[2];
    p[2] = p[3];
    p[3] = parent;
    planned(p, 4, (int[]){0, 1, 2}, 3);

    // The sizes add up, and only the offsets show these two share flash
    layout(p);
    p[2].offset -= ES;
    p[3].offset += ES;
    refused(p, 4, FLASH, "no partition covers 0x240000-0x260000");
    layout(p);
    p[3].offset = p[2].offset;
    p[3].offset += p[2].size - ES;
    refused(p, 4, FLASH, "mtd3 overlaps");

    layout(p);
    p[1].size += 1;
    p[2].offset += 1;
    p[3].offset += 1;
    refused(p, 4, FLASH + 1, "erase blocks");
    layout(p);
    p[3].erasesize = 0x1000;
    refused(p, 4, FLASH, "erase blocks");
    layout(p);
    p[0].erasesize = 0;
    refused(p, 4, FLASH, "erase blocks");

    return failed;
}
