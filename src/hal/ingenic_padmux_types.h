#ifndef HAL_INGENIC_PADMUX_TYPES_H
#define HAL_INGENIC_PADMUX_TYPES_H

#include <stdint.h>

#include "padmux_names.h"

/* The shape of the generated table in ingenic_padmux.h. Separate from it so
 * the generator emits data and nothing else.
 *
 * An Ingenic pad has exactly four device functions and no selector field:
 * which one is live is spelled by one bit each in the port's INT, MSK, PAT1
 * and PAT0 registers, at the pin's own position. So the table is four names
 * and a pad number, and every register lives in ingenic_padmux.c.
 *
 * The names are OFFSETS into padmux_names, for the reason reginfo.h gives at
 * length: a `const char *` here is a relocation in a position-independent
 * consumer, and this table held five of them per pad against 326 distinct
 * names shared across 1,706 uses. padmux_name() turns one back into the
 * `const char *` every caller speaks. */
typedef const struct {
    uint16_t pad;      /* port * 32 + pin, the number the kernel uses */
    uint16_t name;     /* "PB25", the spelling the datasheet uses */
    uint16_t funcs[4]; /* FUNCTION0..3; "reserved" where there is none */
} ingenic_pad_t;

typedef const struct {
    const ingenic_pad_t *pads;
    uint16_t npads;
} ingenic_soc_t;

#endif /* HAL_INGENIC_PADMUX_TYPES_H */
