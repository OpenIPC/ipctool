#ifndef HAL_NOVATEK_PADMUX_TYPES_H
#define HAL_NOVATEK_PADMUX_TYPES_H

#include <stdint.h>

#include "padmux_names.h"

/* The shape of the generated table in novatek_padmux.h. Separate from it so
 * the generator emits data and nothing else.
 *
 * A NA51089 pad is not a selector. What it carries is the answer to two
 * questions in two places: which peripheral's location field in the TOP block
 * names it, and whether the pad's own gate bit has been cleared to hand it
 * over. So a pad's alternatives are CLAIMS -- a name, and the field values
 * under which the pad carries it -- and the first claim whose fields hold is
 * the answer. See src/hal/novatek_padmux.c.
 *
 * Names are offsets into padmux_names, as everywhere else: a pointer here is
 * a relocation in a position-independent consumer. */

/* A claim that holds whatever the pad's gate bit says. The parallel sensor
 * modes put pixel data on the HSI pads and leave the gate at GPIO. */
#define NVT_UNGATED 0x01

/* novatek_sel_t.reg for a walk row whose selector is the gate bit itself. */
#define NVT_GATE 0xFF

/* One TOP-block field and the value it must hold: TOP + reg, `width` bits
 * at `shift`. */
typedef const struct {
    uint8_t reg;
    uint8_t shift;
    uint8_t width;
    uint8_t value;
} novatek_cond_t;

/* One way `pad` can be `name`: conds[cond .. cond + ncond) all hold. */
typedef const struct {
    uint8_t pad;   /* Linux GPIO number */
    uint8_t flags; /* NVT_UNGATED */
    uint16_t name;
    uint16_t cond;
    uint8_t ncond;
} novatek_claim_t;

typedef const struct {
    uint8_t pad;   /* Linux GPIO number */
    uint16_t name; /* "P_GPIO22", the kernel's own macro */
} novatek_pad_t;

/* What a walk row shows as address, mask and value for `name` on `pad`: one
 * field that tells it apart from the pad's other names. Information only --
 * get() and set() read the claims. */
typedef const struct {
    uint8_t pad;
    uint16_t name;
    uint8_t reg; /* or NVT_GATE */
    uint8_t shift;
    uint8_t width;
    uint8_t value;
} novatek_sel_t;

typedef const struct {
    const novatek_pad_t *pads;
    uint16_t npads;
    const novatek_claim_t *claims; /* sorted by pad */
    uint16_t nclaims;
    const novatek_cond_t *conds;
    const novatek_sel_t *sels; /* sorted by pad */
    uint16_t nsels;
} novatek_soc_t;

#endif /* HAL_NOVATEK_PADMUX_TYPES_H */
