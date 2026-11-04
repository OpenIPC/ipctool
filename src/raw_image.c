#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "raw_image.h"

size_t raw_image_plan(const raw_part_t *parts, size_t nparts,
                      uint64_t image_size, raw_part_t *plan, char *why,
                      size_t whylen) {
    if (!nparts) {
        snprintf(why, whylen, "no flash partitions found");
        return 0;
    }

    for (size_t k = 0; k < nparts; k++) {
        if (!parts[k].nor) {
            snprintf(why, whylen,
                     "mtd%d is not NOR: a raw NAND image has no bad block or "
                     "UBI handling here",
                     parts[k].num);
            return 0;
        }
        if (!parts[k].has_offset) {
            snprintf(why, whylen,
                     "cannot tell where mtd%d lies on the flash: reboot and "
                     "run this again while the boot log still says",
                     parts[k].num);
            return 0;
        }
    }

    // By offset; few enough that this is all the sort it needs
    static raw_part_t sorted[64];
    if (nparts > sizeof(sorted) / sizeof(sorted[0])) {
        snprintf(why, whylen, "too many partitions");
        return 0;
    }
    for (size_t k = 0; k < nparts; k++) {
        size_t at = k;
        for (; at && sorted[at - 1].offset > parts[k].offset; at--)
            sorted[at] = sorted[at - 1];
        sorted[at] = parts[k];
    }

    size_t nplan = 0;
    uint64_t covered = 0;
    for (size_t k = 0; k < nparts; k++) {
        const raw_part_t *p = &sorted[k];
        if (p->offset + p->size <= covered)
            continue;
        if (p->offset < covered) {
            snprintf(why, whylen,
                     "mtd%d overlaps the partition before it at 0x%" PRIx64,
                     p->num, p->offset);
            return 0;
        }
        if (p->offset > covered) {
            snprintf(why, whylen,
                     "no partition covers 0x%" PRIx64 "-0x%" PRIx64 ": one is "
                     "missing or read-only",
                     covered, p->offset);
            return 0;
        }
        if (!p->erasesize || p->erasesize != sorted[0].erasesize ||
            p->size % p->erasesize) {
            snprintf(why, whylen,
                     "mtd%d is not a whole number of 0x%" PRIx32 " erase "
                     "blocks",
                     p->num, sorted[0].erasesize);
            return 0;
        }
        plan[nplan++] = *p;
        covered += p->size;
    }

    if (image_size != covered) {
        snprintf(why, whylen,
                 "the image is 0x%" PRIx64 " bytes, the partitions cover "
                 "0x%" PRIx64,
                 image_size, covered);
        return 0;
    }
    return nplan;
}
