#ifndef HAL_INGENIC_PADMUX_TYPES_H
#define HAL_INGENIC_PADMUX_TYPES_H

#include <stdint.h>

/* The shape of the generated table in ingenic_padmux.h. Separate from it so
 * the generator emits data and nothing else.
 *
 * An Ingenic pad has exactly four device functions and no selector field:
 * which one is live is spelled by one bit each in the port's INT, MSK, PAT1
 * and PAT0 registers, at the pin's own position. So the table is four names
 * and a pad number, and every register lives in ingenic_padmux.c. */
typedef const struct {
    uint16_t pad;         /* port * 32 + pin, the number the kernel uses */
    const char *name;     /* "PB25", the spelling the datasheet uses */
    const char *funcs[4]; /* FUNCTION0..3; "reserved" where there is none */
} ingenic_pad_t;

typedef const struct {
    const ingenic_pad_t *pads;
    uint16_t npads;
} ingenic_soc_t;

#endif /* HAL_INGENIC_PADMUX_TYPES_H */
