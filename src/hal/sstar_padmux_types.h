#ifndef HAL_SSTAR_PADMUX_TYPES_H
#define HAL_SSTAR_PADMUX_TYPES_H

#include <stdint.h>

#include "ipchw.h"
#include "padmux_names.h"

/* The shape of the generated tables in sstar_padmux.h. Separate from them so
 * the generator emits data and nothing else.
 *
 * A SigmaStar mode is one field of one 16-bit RIU register plus the value
 * that routes a peripheral onto a group of pads. Every pad claimed by that
 * mode is claimed by the same field and the same value -- that is what lets a
 * pad hold mode indices instead of triples, and tools/gen_sstar_padmux.py
 * proves it over every row of the vendor table rather than assuming it. */
/* `name` is an OFFSET into padmux_names, not a pointer -- see reginfo.h for
 * the arithmetic. This table and the pad table below held 882 name pointers
 * between them, which is 7 KB of relocations in a position-independent
 * consumer. padmux_name() turns one back into a `const char *`. */
typedef const struct {
    uint16_t name;    /* the vendor's own spelling, "I2C1_MODE_3" */
    uint32_t address; /* physical; a 16-bit port in a four-byte slot */
    uint16_t mask;    /* the field, IN PLACE */
    uint16_t val;     /* the value that selects this mode, in place */
} sstar_mode_t;

/* One field with one value in it. Used for the "this pad is GPIO" bits, which
 * belong to the pad rather than to a mode -- infinity6b0 has almost none,
 * because there a pad is GPIO when nothing claims it, and infinity6c
 * sometimes needs two. */
typedef const struct {
    uint32_t address;
    uint16_t mask;
    uint16_t val;
} sstar_field_t;

typedef const struct {
    uint16_t name;  /* "PAD_SR_IO03" -- the vendor's name for the pad */
    uint16_t first; /* first of this pad's entries in the mode pool */
    uint16_t nmodes;
    uint16_t gpio_first; /* first of its entries in the GPIO-field pool */
    uint16_t ngpio;
    uint16_t unnamed_first; /* first of its entries in the unnamed-field pool */
    uint16_t nunnamed;
} sstar_pad_t;

typedef const struct {
    const sstar_mode_t *modes;
    uint16_t nmodes;
    const sstar_pad_t *pads; /* indexed by pad id, which is the GPIO number */
    uint16_t npads;
    const uint16_t *pool;             /* mode indices, per pad */
    const sstar_field_t *gpio_fields; /* GPIO claims, per pad */
    /* Fields that would mean a mode the generator had to drop is live on the
     * pad. Not functions this build can name, just reasons not to call a pad
     * free -- see the comment on them in sstar_padmux.h. */
    const sstar_field_t *unnamed_fields;
} sstar_family_t;

/* The idle value of a field: what the vendor writes to take a claim off a
 * pad. Zero for a mode selected by a non-zero value, and the whole field for
 * one selected by zero -- a handful of the boot-SPI and IR pads are like
 * that, and clearing those would assert the claim rather than drop it. */
static inline uint16_t sstar_idle(uint16_t mask, uint16_t val) {
    return val ? 0 : mask;
}

#endif /* HAL_SSTAR_PADMUX_TYPES_H */
