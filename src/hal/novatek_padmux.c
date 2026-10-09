/* Novatek NA51089 (NT98562/NT98566): the pad-mux backend.
 *
 * Two things decide what a pad carries, and they are in different places:
 *
 *   - a location field per PERIPHERAL in the TOP block. REG5[7:6] is I2C3,
 *     and 1, 2 and 3 put it on P_GPIO21/22, C_GPIO11/12 or DSI_GPIO8/9. It
 *     is SigmaStar's arrangement: the field belongs to the peripheral, and
 *     names a group of pads.
 *   - a gate bit per PAD, in one of eight bitmaps at TOP + 0xA0..0xE8, the
 *     pad's group's word at its pin's position. 1 keeps the pad a GPIO; 0
 *     hands it to whichever peripheral's field names it.
 *
 * The vendor's pinmux_config_*() functions set both, every time. So a pad is
 * GPIO when its gate is set, and otherwise it is whatever the TOP fields say
 * -- which the generated table states as claims: a name, and the field
 * values under which the pad carries it. The first claim that holds wins;
 * the table is sorted most specific first, and the generator replayed every
 * state the vendor code can produce to make that rule agree with it.
 *
 * Two exceptions the claims carry. The parallel and CCIR sensor modes put
 * pixel data on the HSI pads and leave their gate bits at GPIO, so those
 * claims hold through the gate (NVT_UNGATED). And a gate cleared with no
 * claim holding is a pad whose peripheral moved elsewhere afterwards: get()
 * says it cannot name it, which is the truth.
 *
 * Putting a function on a pad is therefore never one write: the claim's
 * fields, then the gate. None of these rows carries IPCHW_PADMUX_F_RMW
 * except the GPIO row of a pad nothing ungated can claim, where setting the
 * gate bit is the whole of it.
 *
 * Registers are in the TOP block at 0xF0010000, which is the DT's
 * "nvt,nvt_top" reg[0]; the table is generated from the vendor's own driver
 * by tools/gen_novatek_padmux.py. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "chipid.h"
#include "hal/novatek.h"
#include "hal/novatek_padmux.h"
#include "padmux.h"

#define TOP_BASE 0xF0010000u

/* A pad's gate register is its group's: the Linux GPIO number's upper three
 * bits pick the group (C, P, S, L, D, H, A, DSI) and the lower five are the
 * bit. */
static const uint8_t gate_regs[8] = {0xA0, 0xA8, 0xB0, 0xB8,
                                     0xD0, 0xD8, 0xE0, 0xE8};

/* What one call needs to know about the TOP block, read once each. */
#define TOP_WORDS (0x100 / 4)

typedef struct {
    const padmux_io_t *io;
    uint32_t val[TOP_WORDS];
    uint8_t have[TOP_WORDS];
    uint32_t orig[TOP_WORDS]; /* what a written word held before, to undo */
    uint8_t dirty[TOP_WORDS];
} top_t;

static const novatek_soc_t *nvt_soc(void) {
    switch (chip_generation) {
    case CHIP_NA51089:
        return &NA51089_padmux;
    default:
        return NULL;
    }
}

static const novatek_pad_t *find_pad(const novatek_soc_t *soc, int pad) {
    for (int i = 0; i < soc->npads; i++)
        if (soc->pads[i].pad == pad)
            return &soc->pads[i];

    return NULL;
}

static uint32_t gate_addr(int pad) { return TOP_BASE + gate_regs[pad >> 5]; }

static uint32_t gate_bit(int pad) { return 1u << (pad & 31); }

static uint32_t field_mask(unsigned shift, unsigned width) {
    return ((1u << width) - 1) << shift;
}

static bool top_read(top_t *t, unsigned reg, uint32_t *val) {
    unsigned w = reg / 4;
    if (!t->have[w]) {
        if (!t->io->read(TOP_BASE + reg, &t->val[w], 32))
            return false;
        t->have[w] = 1;
    }
    *val = t->val[w];
    return true;
}

static bool top_write_field(top_t *t, unsigned reg, uint32_t mask,
                            uint32_t bits) {
    uint32_t val;
    if (!top_read(t, reg, &val))
        return false;
    if (!t->dirty[reg / 4]) {
        t->orig[reg / 4] = val;
        t->dirty[reg / 4] = 1;
    }
    val = (val & ~mask) | (bits & mask);
    if (!t->io->write(TOP_BASE + reg, val, 32))
        return false;
    t->val[reg / 4] = val;
    return true;
}

/* Put back every word this call wrote, for a set() that is refusing: having
 * moved a peripheral's location field half-way to a function it then says it
 * cannot put on the pad would unmux that peripheral from every other pad it
 * was on. Best effort -- an I/O error that stopped the set may stop this. */
static int top_undo(top_t *t, int res) {
    for (unsigned w = 0; w < TOP_WORDS; w++)
        if (t->dirty[w])
            t->io->write(TOP_BASE + w * 4, t->orig[w], 32);
    return res;
}

/* 1 when every condition of `c` holds, 0 when one does not, or
 * IPCHW_PADMUX_IO. */
static int claim_holds(const novatek_soc_t *soc, const novatek_claim_t *c,
                       top_t *t) {
    for (int i = 0; i < c->ncond; i++) {
        const novatek_cond_t *k = &soc->conds[c->cond + i];
        uint32_t val;
        if (!top_read(t, k->reg, &val))
            return IPCHW_PADMUX_IO;
        if (((val >> k->shift) & ((1u << k->width) - 1)) != k->value)
            return 0;
    }
    return 1;
}

/* The claims of `pad`, which the table keeps together. */
static const novatek_claim_t *pad_claims(const novatek_soc_t *soc, int pad,
                                         int *n) {
    *n = 0;
    for (int i = 0; i < soc->nclaims; i++) {
        if (soc->claims[i].pad != pad)
            continue;
        int j = i;
        while (j < soc->nclaims && soc->claims[j].pad == pad)
            j++;
        *n = j - i;
        return &soc->claims[i];
    }
    return NULL;
}

/* What the pad carries now: the claim that wins, or NULL with *gpio set for
 * plain GPIO and clear for a gate cleared with nothing claiming the pad.
 * Negative is IPCHW_PADMUX_IO. */
static int resolve(const novatek_soc_t *soc, int pad, top_t *t,
                   const novatek_claim_t **win, bool *gpio) {
    uint32_t gate;
    if (!top_read(t, gate_regs[pad >> 5], &gate))
        return IPCHW_PADMUX_IO;
    bool gated = (gate & gate_bit(pad)) != 0;

    *win = NULL;
    *gpio = gated;

    int n;
    const novatek_claim_t *c = pad_claims(soc, pad, &n);
    for (int i = 0; i < n; i++) {
        if (gated && !(c[i].flags & NVT_UNGATED))
            continue;
        int res = claim_holds(soc, &c[i], t);
        if (res < 0)
            return res;
        if (res) {
            *win = &c[i];
            *gpio = false;
            return 0;
        }
    }
    return 0;
}

static bool has_ungated(const novatek_soc_t *soc, int pad) {
    int n;
    const novatek_claim_t *c = pad_claims(soc, pad, &n);
    for (int i = 0; i < n; i++)
        if (c[i].flags & NVT_UNGATED)
            return true;
    return false;
}

static ipchw_padmux_t gpio_row(const novatek_soc_t *soc,
                               const novatek_pad_t *pd) {
    uint32_t bit = gate_bit(pd->pad);
    bool ungated = has_ungated(soc, pd->pad);

    return (ipchw_padmux_t){
        .address = gate_addr(pd->pad),
        .func_mask = bit,
        .func = (int)bit,
        .func_name = IPCHW_PADMUX_GPIO,
        .gpio_name = padmux_name(pd->name),
        .gpio_pad = pd->pad,
        /* Setting the gate is the whole operation -- unless a sensor mode
         * can hold the pad through it, and then it is not, and a consumer
         * has to go through ipchw_padmux_set(). */
        .gpio_func = ungated ? -1 : (int)bit,
        .flags = IPCHW_PADMUX_F_GPIO | (ungated ? 0 : IPCHW_PADMUX_F_RMW),
    };
}

static ipchw_padmux_t func_row(const novatek_pad_t *pd,
                               const novatek_sel_t *s) {
    ipchw_padmux_t row = {
        .func_name = padmux_name(s->name),
        .gpio_name = padmux_name(pd->name),
        .gpio_pad = pd->pad,
        .gpio_func = -1,
        .flags = 0,
    };

    if (s->reg == NVT_GATE) {
        /* Told apart from the pad's other names by nothing but the gate
         * being clear -- the MIPI lanes, which no field selects. */
        row.address = gate_addr(pd->pad);
        row.func_mask = gate_bit(pd->pad);
        row.func = 0;
    } else {
        row.address = TOP_BASE + s->reg;
        row.func_mask = field_mask(s->shift, s->width);
        row.func = (int)((uint32_t)s->value << s->shift);
    }
    return row;
}

static const novatek_sel_t *find_sel(const novatek_soc_t *soc, int pad,
                                     uint16_t name) {
    for (int i = 0; i < soc->nsels; i++)
        if (soc->sels[i].pad == pad && soc->sels[i].name == name)
            return &soc->sels[i];

    return NULL;
}

static int nvt_walk(padmux_match_fn match, const void *arg, int pad,
                    ipchw_padmux_t *out, int max) {
    const novatek_soc_t *soc = nvt_soc();
    if (soc == NULL)
        return IPCHW_PADMUX_NO_TABLE;

    int found = 0;
    for (int i = 0; i < soc->npads; i++) {
        const novatek_pad_t *pd = &soc->pads[i];
        if (pad >= 0 && pd->pad != pad)
            continue;

        if (match == NULL || match(IPCHW_PADMUX_GPIO, arg)) {
            if (found < max)
                out[found] = gpio_row(soc, pd);
            found++;
        }

        for (int s = 0; s < soc->nsels; s++) {
            const novatek_sel_t *sel = &soc->sels[s];
            if (sel->pad != pd->pad)
                continue;
            if (match != NULL && !match(padmux_name(sel->name), arg))
                continue;

            if (found < max)
                out[found] = func_row(pd, sel);
            found++;
        }
    }

    return found;
}

static int nvt_get(int pad, ipchw_padmux_t *out, const padmux_io_t *io) {
    const novatek_soc_t *soc = nvt_soc();
    if (soc == NULL)
        return IPCHW_PADMUX_NO_TABLE;

    const novatek_pad_t *pd = find_pad(soc, pad);
    if (pd == NULL)
        return IPCHW_PADMUX_NO_PAD;

    top_t t = {.io = io};
    const novatek_claim_t *win;
    bool gpio;
    int res = resolve(soc, pad, &t, &win, &gpio);
    if (res < 0)
        return res;

    if (win != NULL) {
        const novatek_sel_t *s = find_sel(soc, pad, win->name);
        if (s == NULL)
            return 0;
        *out = func_row(pd, s);
        return 1;
    }
    if (gpio) {
        *out = gpio_row(soc, pd);
        return 1;
    }
    return 0; /* handed over, and nothing claims it */
}

/* Whether `k` is one of `keep`'s conditions: a field the function being put
 * on the pad needs, and so one no competitor may be dropped through. */
static bool pinned(const novatek_soc_t *soc, const novatek_claim_t *keep,
                   const novatek_cond_t *k) {
    if (keep == NULL)
        return false;
    for (int i = 0; i < keep->ncond; i++) {
        const novatek_cond_t *p = &soc->conds[keep->cond + i];
        if (p->reg == k->reg && p->shift == k->shift && p->width == k->width)
            return true;
    }
    return false;
}

/* Get `pad` to read as `target` (NULL: plain GPIO) once the target's own
 * writes are done. Whatever still wins instead is a claim of something
 * else, and it is dropped by clearing one of its fields that the target does
 * not need -- SigmaStar drops a competing mode the same way. Bounded,
 * because each pass clears a field for good. */
static int drop_competitors(const novatek_soc_t *soc, int pad, top_t *t,
                            const novatek_claim_t *target) {
    for (int pass = 0; pass < 8; pass++) {
        const novatek_claim_t *win;
        bool gpio;
        int res = resolve(soc, pad, t, &win, &gpio);
        if (res < 0)
            return res;
        if (target == NULL ? gpio : (win && win->name == target->name))
            return 0;
        if (win == NULL)
            return IPCHW_PADMUX_NO_FUNC; /* only the gate could; it can't */

        const novatek_cond_t *drop = NULL;
        for (int i = 0; i < win->ncond && drop == NULL; i++) {
            const novatek_cond_t *k = &soc->conds[win->cond + i];
            if (k->value != 0 && !pinned(soc, target, k))
                drop = k;
        }
        if (drop == NULL)
            return IPCHW_PADMUX_NO_FUNC;
        if (!top_write_field(t, drop->reg, field_mask(drop->shift, drop->width),
                             0))
            return IPCHW_PADMUX_IO;
    }
    return IPCHW_PADMUX_NO_FUNC;
}

/* How many of `c`'s conditions do not hold now; negative on I/O error. */
static int unmet(const novatek_soc_t *soc, const novatek_claim_t *c, top_t *t) {
    int n = 0;
    for (int i = 0; i < c->ncond; i++) {
        const novatek_cond_t *k = &soc->conds[c->cond + i];
        uint32_t val;
        if (!top_read(t, k->reg, &val))
            return IPCHW_PADMUX_IO;
        if (((val >> k->shift) & ((1u << k->width) - 1)) != k->value)
            n++;
    }
    return n;
}

static int nvt_set(int pad, const char *func_name, const padmux_io_t *io) {
    const novatek_soc_t *soc = nvt_soc();
    if (soc == NULL)
        return IPCHW_PADMUX_NO_TABLE;

    const novatek_pad_t *pd = find_pad(soc, pad);
    if (pd == NULL)
        return IPCHW_PADMUX_NO_PAD;

    top_t t = {.io = io};
    unsigned gr = gate_regs[pad >> 5];

    if (!strcmp(func_name, IPCHW_PADMUX_GPIO) ||
        !strcmp(func_name, padmux_name(pd->name))) {
        if (!top_write_field(&t, gr, gate_bit(pad), gate_bit(pad)))
            return top_undo(&t, IPCHW_PADMUX_IO);
        int res = drop_competitors(soc, pad, &t, NULL);
        return res ? top_undo(&t, res) : 0;
    }

    /* Of the claims that spell this name, the one that changes least: on a
     * MIPI board SENSOR_MCLK is the claim made under the CSI sensor mode, and
     * the one made under the parallel mode would switch the sensor off. */
    int n;
    const novatek_claim_t *c = pad_claims(soc, pad, &n);
    const novatek_claim_t *best = NULL;
    int best_unmet = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(padmux_name(c[i].name), func_name) != 0)
            continue;
        int u = unmet(soc, &c[i], &t);
        if (u < 0)
            return u;
        if (best == NULL || u < best_unmet ||
            (u == best_unmet && c[i].ncond < best->ncond)) {
            best = &c[i];
            best_unmet = u;
        }
    }
    if (best == NULL)
        return IPCHW_PADMUX_NO_FUNC;

    for (int i = 0; i < best->ncond; i++) {
        const novatek_cond_t *k = &soc->conds[best->cond + i];
        if (!top_write_field(&t, k->reg, field_mask(k->shift, k->width),
                             (uint32_t)k->value << k->shift))
            return top_undo(&t, IPCHW_PADMUX_IO);
    }
    if (!(best->flags & NVT_UNGATED) &&
        !top_write_field(&t, gr, gate_bit(pad), 0))
        return top_undo(&t, IPCHW_PADMUX_IO);

    int res = drop_competitors(soc, pad, &t, best);
    return res ? top_undo(&t, res) : 0;
}

/* "P_GPIO22", "C_GPIO17" or the vendor's other spellings of it, "MC17" and
 * "HSI_GPIO9", or the Linux GPIO number. Not "5_2": that is HiSilicon's
 * bank-of-eight numbering and would land on a different pad here. */
static int nvt_parse_pad(const char *spec) {
    static const struct {
        const char *prefix;
        int base;
    } groups[] = {
        {"C_GPIO", 0x00},   {"MC", 0x00},       {"P_GPIO", 0x20},
        {"S_GPIO", 0x40},   {"L_GPIO", 0x60},   {"D_GPIO", 0x80},
        {"H_GPIO", 0xA0},   {"HSI_GPIO", 0xA0}, {"A_GPIO", 0xC0},
        {"DSI_GPIO", 0xE0},
    };

    for (size_t i = 0; i < sizeof(groups) / sizeof(*groups); i++) {
        size_t len = strlen(groups[i].prefix);
        if (strncasecmp(spec, groups[i].prefix, len) != 0)
            continue;
        char *tail;
        long n = strtol(spec + len, &tail, 10);
        if (tail == spec + len || *tail || n < 0 || n > 31)
            return -1;
        return groups[i].base + (int)n;
    }

    if (strchr(spec, '_'))
        return -1;

    char *tail;
    long n = strtol(spec, &tail, 10);
    if (tail == spec || *tail || n < 0 || n > 0xff)
        return -1;
    return (int)n;
}

const padmux_ops_t PADMUX_OPS_NOVATEK = {
    .name = "novatek",
    .walk = nvt_walk,
    .get = nvt_get,
    .set = nvt_set,
    .parse_pad = nvt_parse_pad,
};
