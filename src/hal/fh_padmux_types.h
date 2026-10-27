#ifndef HAL_FH_PADMUX_TYPES_H
#define HAL_FH_PADMUX_TYPES_H

#include <stdint.h>

#include "padmux_names.h"

/* The shape of the generated tables in fh_padmux.h. Separate from them so the
 * generator emits data and nothing else.
 *
 * One row per (pad, selector) pair, grouped by pad and in selector order
 * within it. `gpio` is the number the pad's GPIO function carries -- the "9"
 * of GPIO9, which is what the kernel exports it as -- on every row of that
 * pad, or -1 for the few pads with no GPIO function at all. `name` is an
 * OFFSET into padmux_names, not a pointer: see reginfo.h for why. */
typedef const struct {
    uint8_t pad; /* the vendor's pad id: the register is base + 4 * pad */
    uint8_t sel; /* the value of the pad's four-bit function field */
    int8_t gpio;
    uint16_t name;
} fh_func_t;

#endif /* HAL_FH_PADMUX_TYPES_H */
