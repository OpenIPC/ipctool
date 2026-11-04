#include <inttypes.h>
#include <stdio.h>

#include "raw_image.h"

bool raw_image_fits(const raw_part_t *parts, size_t nparts, uint64_t image_size,
                    char *why, size_t whylen) {
    if (!nparts) {
        snprintf(why, whylen, "no flash partitions found");
        return false;
    }

    uint64_t covered = 0;
    for (size_t k = 0; k < nparts; k++) {
        const raw_part_t *p = &parts[k];
        if (!p->nor) {
            snprintf(why, whylen,
                     "mtd%d is not NOR: a raw NAND image has no bad block or "
                     "UBI handling here",
                     p->num);
            return false;
        }
        // Partitions are written by device number and placed by adding up
        // sizes, so the two have to agree. A gap is also what a partition
        // that cannot be opened for writing leaves behind.
        if (p->num != (int)k) {
            snprintf(why, whylen,
                     "found mtd%d where mtd%zu belongs: a partition is "
                     "missing, read-only or out of flash order",
                     p->num, k);
            return false;
        }
        if (!p->erasesize || p->erasesize != parts[0].erasesize ||
            p->size % p->erasesize) {
            snprintf(why, whylen,
                     "mtd%d is not a whole number of 0x%" PRIx32 " erase "
                     "blocks",
                     p->num, parts[0].erasesize);
            return false;
        }
        if (p->has_offset && p->offset != covered) {
            snprintf(why, whylen,
                     "mtd%d starts at 0x%" PRIx64 ", not 0x%" PRIx64 ": the "
                     "partitions do not follow one another",
                     p->num, p->offset, covered);
            return false;
        }
        covered += p->size;
    }

    if (image_size != covered) {
        snprintf(why, whylen,
                 "the image is 0x%" PRIx64 " bytes, the partitions cover "
                 "0x%" PRIx64,
                 image_size, covered);
        return false;
    }
    return true;
}
