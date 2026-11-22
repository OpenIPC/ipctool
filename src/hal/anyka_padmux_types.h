#ifndef HAL_ANYKA_PADMUX_TYPES_H
#define HAL_ANYKA_PADMUX_TYPES_H

#include <stdint.h>

#include "padmux_names.h"

/* The shape of the generated tables in anyka_padmux.h. Separate from them so
 * the generator emits data and nothing else.
 *
 * One row per (pad, selector) pair, grouped by pad and in selector order
 * within it. The pad number is the GPIO number: pad 16 is GPIO16. `reg` is
 * which share-pin register holds the pad's three-bit field, 0xFF for a pad
 * with GPIO alone and no field. `name` is an OFFSET into padmux_names, not a
 * pointer: see reginfo.h for why. */
typedef const struct {
    uint8_t pad;
    uint8_t reg;   /* the register is 0x08000178 + 4 * reg */
    uint8_t shift; /* the field's lowest bit */
    uint8_t sel;
    uint16_t name;
} ak_func_t;

#endif /* HAL_ANYKA_PADMUX_TYPES_H */
