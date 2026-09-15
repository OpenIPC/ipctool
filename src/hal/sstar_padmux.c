/* SigmaStar Infinity6: the pad-mux backend.
 *
 * The mux is per PERIPHERAL, not per pad. `reg_fuart_mode` is three bits of
 * one chip-top register and its value picks which group of pads carries the
 * fast UART -- 1 for the four PAD_FUART_* pads, 2 for PAD_GPIO0..3, 4 for
 * PAD_SD1_IO0..3. So one pad's alternatives are fields in different
 * registers, there is no list to index with a selector, and a pad is plain
 * GPIO when nothing claims it.
 *
 * Which is why "what is this pad carrying" is a scan and "put this on it" is
 * a write plus a sweep that takes the other claims off, and why both have to
 * live behind ipchw_padmux_get()/_set() rather than in a row a caller acts
 * on. The tables are generated: see tools/gen_sstar_padmux.py.
 *
 * Not covered: the pads the vendor's own driver will not program from the
 * table either -- HalPadSetMode_MISC handles the SAR, ETH and USB pads with
 * several banks and a 0xBABE PM unlock. Their composite modes are dropped by
 * the generator, so those pads list their GPIO row and nothing else. */

#include <string.h>

#include "chipid.h"
#include "hal/sstar.h"
#include "hal/sstar_padmux.h"
#include "padmux.h"

/* These are 16-bit RIU ports sitting in four-byte slots whose upper half is
 * not mapped. A 32-bit store would write two bytes that do not exist. */
#define SSTAR_PORT_BITS 16

static const sstar_family_t *sstar_family(void) {
    switch (chip_generation) {
    case INFINITY6:
    case INFINITY6B:
        /* One table: infinity6/mhal_pinmux.c and infinity6b0/mhal_pinmux.c
         * are byte-identical in the vendor tree. */
        return &I6B_padmux;
    case INFINITY6E:
        return &I6E_padmux;
    case INFINITY6C:
        return &I6C_padmux;
    default:
        return NULL;
    }
}

/* A pad this build has nothing at all to say about.
 *
 * The vendor's own driver does not program these from its table either: the
 * SAR, ETH and USB pads, and PM_GPIO4, are a hand-written switch wanting
 * several banks and a 0xBABE unlock. Reporting one as plain GPIO because no
 * claim we know of is asserted would be a lie with consequences -- a pin page
 * would offer PAD_ETH_RN as a free wire to drive. They answer nothing. */
static bool pad_is_dark(const sstar_pad_t *pd) {
    return pd->nmodes == 0 && pd->ngpio == 0 && pd->nunnamed == 0;
}

static const sstar_mode_t *pad_mode(const sstar_family_t *fam,
                                    const sstar_pad_t *pd, int k) {
    return &fam->modes[fam->pool[pd->first + k]];
}

/* The pad's plain-GPIO alternative.
 *
 * Never IPCHW_PADMUX_F_RMW, even on the parts that have a "this pad is GPIO"
 * bit: getting back to GPIO means taking every other claim off the pad as
 * well, and how many of those there are is not knowable from one row. A
 * consumer that composes its own write therefore refuses instead of doing
 * half the job, and ipchw_padmux_set() does all of it. */
static ipchw_padmux_t gpio_row(const sstar_pad_t *pd, int pad) {
    return (ipchw_padmux_t){
        .address = IPCHW_PADMUX_ADDR_NONE,
        .func_mask = 0,
        .func = -1,
        .func_name = IPCHW_PADMUX_GPIO,
        .gpio_name = pd->name,
        .gpio_pad = pad,
        .gpio_func = -1,
        .flags = IPCHW_PADMUX_F_GPIO,
    };
}

static ipchw_padmux_t mode_row(const sstar_mode_t *m, const sstar_pad_t *pd,
                               int pad) {
    return (ipchw_padmux_t){
        .address = m->address,
        .func_mask = m->mask,
        .func = m->val,
        .func_name = m->name,
        .gpio_name = pd->name,
        .gpio_pad = pad,
        /* No single value hands the pad back: see gpio_row(). */
        .gpio_func = -1,
        /* One field, one value -- but the pad's other alternatives are in
         * other registers, so IPCHW_PADMUX_F_SHARED_REG is not true here. */
        .flags = IPCHW_PADMUX_F_RMW,
    };
}

static int sstar_walk(padmux_match_fn match, const void *arg, int pad,
                      ipchw_padmux_t *out, int max) {
    const sstar_family_t *fam = sstar_family();
    if (fam == NULL)
        return IPCHW_PADMUX_NO_TABLE;

    int found = 0;
    for (int p = 0; p < fam->npads; p++) {
        if (pad >= 0 && p != pad)
            continue;

        const sstar_pad_t *pd = &fam->pads[p];
        if (pad_is_dark(pd))
            continue;

        if (match == NULL || match(IPCHW_PADMUX_GPIO, arg)) {
            if (found < max)
                out[found] = gpio_row(pd, p);
            found++;
        }

        for (int k = 0; k < pd->nmodes; k++) {
            const sstar_mode_t *m = pad_mode(fam, pd, k);
            if (match != NULL && !match(m->name, arg))
                continue;

            if (found < max)
                out[found] = mode_row(m, pd, p);
            found++;
        }
    }

    return found;
}

/* Which claim on this pad is live.
 *
 * Two passes, because a few modes are selected by a ZERO field -- the boot
 * SPI and IR pads are like that -- and a zero field is also what an idle one
 * reads as. A positive claim wins; a zero-valued one is only believed when
 * nothing else on the pad is asserted. */
static int sstar_get(int pad, ipchw_padmux_t *out, const padmux_io_t *io) {
    const sstar_family_t *fam = sstar_family();
    if (fam == NULL)
        return IPCHW_PADMUX_NO_TABLE;
    if (pad < 0 || pad >= fam->npads)
        return IPCHW_PADMUX_NO_PAD;

    const sstar_pad_t *pd = &fam->pads[pad];
    if (pad_is_dark(pd))
        return 0;

    const sstar_mode_t *live = NULL; /* what the pad is carrying */
    int positive = 0;                /* how many non-zero claims are asserted */

    for (int pass = 0; pass < 2; pass++) {
        for (int k = 0; k < pd->nmodes; k++) {
            const sstar_mode_t *m = pad_mode(fam, pd, k);
            if ((m->val != 0) != (pass == 0))
                continue;

            uint32_t val;
            if (!io->read(m->address, &val, SSTAR_PORT_BITS))
                return IPCHW_PADMUX_IO;
            if ((val & m->mask) != m->val)
                continue;

            if (m->val != 0)
                positive++;
            if (live == NULL)
                live = m;
        }
        if (live != NULL && pass == 0)
            break; /* a positive claim settles it; zero-valued ones are the
                    * idle reading of a field as much as a claim on it */
    }

    if (live != NULL) {
        *out = mode_row(live, pd, pad);

        /* The table cannot say which value restores GPIO on this family,
         * because GPIO is the absence of every claim rather than a value --
         * which is why the lookups leave gpio_func at -1. Reading the
         * registers can, though, and this function just did: if exactly one
         * field is claiming the pad, clearing THAT field is the whole job,
         * and (address, func_mask, gpio_func) is a single write a caller can
         * compose the way it does on HiSilicon.
         *
         * Two conditions have to hold. Only one positive claim, or clearing
         * this one leaves the pad on the other. And no separate "this pad is
         * GPIO" field to assert as well -- infinity6/6b0 has none at all, so
         * every pad there qualifies, while nearly every infinity6c and
         * infinity6e pad has one and needs the second write that
         * ipchw_padmux_set() does. */
        if (pd->ngpio == 0 && positive == 1 && live->val != 0)
            out->gpio_func = (int)sstar_idle(live->mask, live->val);

        return 1;
    }

    /* Nothing this build can NAME claims the pad. Two things still might.
     *
     * First, a mode the generator had to drop because the same mode id
     * selects a different register on different pads. Losing the name does
     * not have to mean losing the question: the pad keeps the field that
     * would mean that mode is live, and one of them reading back its value
     * says the pad is carrying something unnameable rather than lying idle.
     * Only claims selected by a NON-ZERO value are here -- one selected by
     * zero cannot be told from an idle register, and the pads that have those
     * fall to the check below instead. */
    for (int u = 0; u < pd->nunnamed; u++) {
        const sstar_field_t *f = &fam->unnamed_fields[pd->unnamed_first + u];
        uint32_t val;
        if (!io->read(f->address, &val, SSTAR_PORT_BITS))
            return IPCHW_PADMUX_IO;
        if ((val & f->mask) == f->val)
            return 0;
    }

    /* Second, for a pad with no named modes at all, its own "this pad is
     * GPIO" field -- the only check left when everything it could be was
     * dropped, which is the infinity6c Ethernet and USB pads. A pad whose
     * alternatives ARE all in the table needs no such check: no mode asserted
     * means no peripheral, so it is the GPIO the part falls back to, and
     * asking anyway hid 34 of an SSC377D's 86 pads. */
    for (int g = 0; pd->nmodes == 0 && g < pd->ngpio; g++) {
        const sstar_field_t *f = &fam->gpio_fields[pd->gpio_first + g];
        uint32_t val;
        if (!io->read(f->address, &val, SSTAR_PORT_BITS))
            return IPCHW_PADMUX_IO;
        if ((val & f->mask) != f->val)
            return 0; /* not GPIO, and not anything this build can name */
    }

    *out = gpio_row(pd, pad);
    return 1;
}

static int write_field(const padmux_io_t *io, uint32_t address, uint16_t mask,
                       uint16_t val) {
    uint32_t old;

    if (!io->read(address, &old, SSTAR_PORT_BITS))
        return IPCHW_PADMUX_IO;

    uint32_t want = (old & ~(uint32_t)mask) | val;
    if (want == old)
        return 0;
    if (!io->write(address, want, SSTAR_PORT_BITS))
        return IPCHW_PADMUX_IO;

    return 0;
}

/* Take a claim off the pad, but only if it is actually asserted -- these
 * fields are shared with the other pads of the same group, and clearing one
 * that is routing a peripheral elsewhere would mux that peripheral away from
 * a pad nobody asked about. */
static int drop_field(const padmux_io_t *io, uint32_t address, uint16_t mask,
                      uint16_t val) {
    uint32_t old;

    if (!io->read(address, &old, SSTAR_PORT_BITS))
        return IPCHW_PADMUX_IO;
    if ((old & mask) != val)
        return 0;

    return write_field(io, address, mask, sstar_idle(mask, val));
}

static int sstar_set(int pad, const char *func_name, const padmux_io_t *io) {
    const sstar_family_t *fam = sstar_family();
    if (fam == NULL)
        return IPCHW_PADMUX_NO_TABLE;
    if (pad < 0 || pad >= fam->npads)
        return IPCHW_PADMUX_NO_PAD;

    const sstar_pad_t *pd = &fam->pads[pad];
    if (pad_is_dark(pd))
        return IPCHW_PADMUX_NO_FUNC;

    bool to_gpio =
        !strcmp(func_name, IPCHW_PADMUX_GPIO) || !strcmp(func_name, pd->name);

    int want = -1;
    if (!to_gpio) {
        for (int k = 0; k < pd->nmodes; k++)
            if (!strcmp(pad_mode(fam, pd, k)->name, func_name)) {
                want = k;
                break;
            }
        if (want < 0)
            return IPCHW_PADMUX_NO_FUNC;
    }

    /* Drop first, then assert. The vendor's driver interleaves the two in
     * table order; doing it in this order means the pad is never claimed
     * twice at once, which matters on a live camera rather than on paper. */
    for (int k = 0; k < pd->nmodes; k++) {
        if (k == want)
            continue;
        const sstar_mode_t *m = pad_mode(fam, pd, k);
        int res = drop_field(io, m->address, m->mask, m->val);
        if (res != 0)
            return res;
    }

    for (int g = 0; g < pd->ngpio; g++) {
        const sstar_field_t *f = &fam->gpio_fields[pd->gpio_first + g];
        int res = to_gpio ? write_field(io, f->address, f->mask, f->val)
                          : drop_field(io, f->address, f->mask, f->val);
        if (res != 0)
            return res;
    }

    if (want >= 0) {
        const sstar_mode_t *m = pad_mode(fam, pd, want);
        int res = write_field(io, m->address, m->mask, m->val);
        if (res != 0)
            return res;
    }

    return 0;
}

const padmux_ops_t PADMUX_OPS_SSTAR = {
    .name = "sstar",
    .walk = sstar_walk,
    .get = sstar_get,
    .set = sstar_set,
};
