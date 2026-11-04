#ifndef RAW_IMAGE_H
#define RAW_IMAGE_H

/*
 * The pure half of `ipctool restore --raw`: how an image with no description
 * of its own is cut along this camera's partitions. Kept apart so it can be
 * tested on a host (raw_image_test.c).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int num; // N of /dev/mtdN
    uint64_t size;
    uint32_t erasesize;
    bool nor;
    // Where it starts on the flash, if anything could say
    bool has_offset;
    uint64_t offset;
} raw_part_t;

/*
 * Picks the partitions that cover an image of `image_size` bytes exactly once
 * and puts them in `plan`, which holds `nparts`, in flash order. Returns how
 * many, or 0 with `why` saying what is in the way. A partition that lies
 * wholly inside the ones picked is another name for them and is left out.
 * There is no override: an image that does not fit is the wrong image, and a
 * layout that cannot be established cannot be written safely.
 */
size_t raw_image_plan(const raw_part_t *parts, size_t nparts,
                      uint64_t image_size, raw_part_t *plan, char *why,
                      size_t whylen);

#endif /* RAW_IMAGE_H */
