#ifndef RAW_IMAGE_H
#define RAW_IMAGE_H

/*
 * The pure half of `ipctool restore --raw`: whether an image with no
 * description of its own can be cut along this camera's partitions. Kept
 * apart so it can be tested on a host (raw_image_test.c).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int num; // N of /dev/mtdN
    uint64_t size;
    uint32_t erasesize;
    bool nor;
    // Where the kernel says the partition starts, on kernels that say
    bool has_offset;
    uint64_t offset;
} raw_part_t;

/*
 * `parts` in the order they lie on the flash. True when an image of
 * `image_size` bytes maps onto them one to one; otherwise `why` says what is
 * in the way. There is no override: an image that does not fit is the wrong
 * image, or a layout the offsets cannot be trusted on.
 */
bool raw_image_fits(const raw_part_t *parts, size_t nparts, uint64_t image_size,
                    char *why, size_t whylen);

#endif /* RAW_IMAGE_H */
