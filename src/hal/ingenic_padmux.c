/* Ingenic T-series: the pad-mux backend.
 *
 * A pad has four device functions, and which one is live is not a field
 * anywhere. Four registers per port -- INT, MSK, PAT1, PAT0 -- carry one bit
 * each at the pin's own position, and the nibble they spell is what the
 * vendor's soc/gpio.h calls enum gpio_function:
 *
 *   0..3   the four device functions       INT 0, MSK 0, PAT1:PAT0 = n
 *   4, 5   GPIO driven low, driven high    INT 0, MSK 1, PAT1 0, PAT0 = level
 *   6      GPIO input                      INT 0, MSK 1, PAT1 1
 *   8..11  an interrupt source             INT 1
 *
 * So no read-modify-write selects a function, which is why these rows do not
 * carry IPCHW_PADMUX_F_RMW and why the whole operation has to be behind
 * ipchw_padmux_set(). Nor is it four ordinary stores: each register has a set
 * and a clear alias at +4 and +8 so one pin can be changed without reading
 * the other thirty-one, and the writes are staged until the port's group
 * number is written to PZGID2LD at +0xF0, which commits them together.
 * Without that commit a pad would pass through every intermediate nibble, and
 * half of those nibbles are other device functions -- on a console pad that
 * is a garbage character on somebody's terminal.
 *
 * This is gpio_set_func() in the vendor's arch/mips/xburst/soc-t31/common/
 * gpio.c, which writes the sets, then the clears, then the group number.
 *
 * The names are generated: see tools/gen_ingenic_padmux.py. */

#include <string.h>

#include "chipid.h"
#include "hal/ingenic.h"
#include "hal/ingenic_padmux.h"
#include "padmux.h"

#define GPIO_BASE 0x10010000u
#define PORT_STRIDE 0x1000u

#define REG_INT 0x10
#define REG_MSK 0x20
#define REG_PAT1 0x30
#define REG_PAT0 0x40
#define REG_GID2LD 0xF0
#define REG_SET(x) ((x) + 0x4)
#define REG_CLEAR(x) ((x) + 0x8)

/* The four bits, most significant first, as enum gpio_function spells them. */
#define FUNC_INT 0x8
#define FUNC_MSK 0x4
#define FUNC_PAT1 0x2
#define FUNC_PAT0 0x1

#define GPIO_INPUT 0x6

static const ingenic_soc_t *ingenic_soc(void) {
    switch (chip_generation) {
    case T31:
        return &T31_padmux;
    default:
        return NULL;
    }
}

static const ingenic_pad_t *find_pad(const ingenic_soc_t *soc, int pad) {
    for (int i = 0; i < soc->npads; i++)
        if (soc->pads[i].pad == pad)
            return &soc->pads[i];

    return NULL;
}

static uint32_t port_reg(int pad, unsigned off) {
    return GPIO_BASE + (uint32_t)(pad / 32) * PORT_STRIDE + off;
}

static ipchw_padmux_t pad_row(const ingenic_pad_t *pd, int func) {
    return (ipchw_padmux_t){
        /* Every alternative of this pad is four bits in four registers, so
         * there is no address a caller could usefully write. Saying so with
         * the sentinel rather than a plausible-looking register is what makes
         * an unaware consumer's read-modify-write inert. */
        .address = IPCHW_PADMUX_ADDR_NONE,
        .func_mask = 0,
        .func = func,
        .func_name = func < 0 ? IPCHW_PADMUX_GPIO : pd->funcs[func],
        .gpio_name = pd->name,
        .gpio_pad = pd->pad,
        .gpio_func = -1,
        .flags = func < 0 ? IPCHW_PADMUX_F_GPIO : 0,
    };
}

static int ingenic_walk(padmux_match_fn match, const void *arg, int pad,
                        ipchw_padmux_t *out, int max) {
    const ingenic_soc_t *soc = ingenic_soc();
    if (soc == NULL)
        return IPCHW_PADMUX_NO_TABLE;

    int found = 0;
    for (int i = 0; i < soc->npads; i++) {
        const ingenic_pad_t *pd = &soc->pads[i];
        if (pad >= 0 && pd->pad != pad)
            continue;

        if (match == NULL || match(IPCHW_PADMUX_GPIO, arg)) {
            if (found < max)
                out[found] = pad_row(pd, -1);
            found++;
        }

        for (int f = 0; f < 4; f++) {
            if (!strcmp(pd->funcs[f], "reserved"))
                continue;
            if (match != NULL && !match(pd->funcs[f], arg))
                continue;

            if (found < max)
                out[found] = pad_row(pd, f);
            found++;
        }
    }

    return found;
}

/* The pin's four bits, as one nibble. */
static int read_code(int pad, const padmux_io_t *io, unsigned *code) {
    static const unsigned regs[4] = {REG_INT, REG_MSK, REG_PAT1, REG_PAT0};
    uint32_t bit = 1u << (pad % 32);

    *code = 0;
    for (int i = 0; i < 4; i++) {
        uint32_t val;
        if (!io->read(port_reg(pad, regs[i]), &val, 32))
            return IPCHW_PADMUX_IO;
        *code = (*code << 1) | ((val & bit) ? 1 : 0);
    }

    return 0;
}

static int ingenic_get(int pad, ipchw_padmux_t *out, const padmux_io_t *io) {
    const ingenic_soc_t *soc = ingenic_soc();
    if (soc == NULL)
        return IPCHW_PADMUX_NO_TABLE;

    const ingenic_pad_t *pd = find_pad(soc, pad);
    if (pd == NULL)
        return IPCHW_PADMUX_NO_PAD;

    unsigned code;
    int res = read_code(pad, io, &code);
    if (res != 0)
        return res;

    /* INT or MSK set means the pin belongs to the GPIO block -- an input, an
     * output, or an interrupt source. Which of those is a question about the
     * pin's direction and level, not about its mux. */
    if (code & (FUNC_INT | FUNC_MSK)) {
        *out = pad_row(pd, -1);
        return 1;
    }

    int func = (int)(code & (FUNC_PAT1 | FUNC_PAT0));
    if (!strcmp(pd->funcs[func], "reserved"))
        return 0; /* on a function the spec says this pad does not have */

    *out = pad_row(pd, func);
    return 1;
}

/* Stage one bit through its set or clear alias, which leaves the other
 * thirty-one pins of the port alone. */
static int stage_bit(int pad, unsigned reg, bool set, const padmux_io_t *io) {
    uint32_t addr = port_reg(pad, set ? REG_SET(reg) : REG_CLEAR(reg));

    return io->write(addr, 1u << (pad % 32), 32) ? 0 : IPCHW_PADMUX_IO;
}

/* Put one nibble of enum gpio_function on the pin, atomically.
 *
 * Sets first, then clears, then the port's group number to PZGID2LD, which is
 * the order the vendor's gpio_set_func() uses and the one that commits all
 * four bits together. */
static int write_code(int pad, unsigned code, const padmux_io_t *io) {
    static const unsigned regs[4] = {REG_INT, REG_MSK, REG_PAT1, REG_PAT0};
    static const unsigned bits[4] = {FUNC_INT, FUNC_MSK, FUNC_PAT1, FUNC_PAT0};

    for (int pass = 0; pass < 2; pass++) {
        bool set = pass == 0;
        for (int i = 0; i < 4; i++) {
            if (((code & bits[i]) != 0) != set)
                continue;
            int res = stage_bit(pad, regs[i], set, io);
            if (res != 0)
                return res;
        }
    }

    if (!io->write(port_reg(pad, REG_GID2LD), (uint32_t)(pad / 32), 32))
        return IPCHW_PADMUX_IO;

    return 0;
}

static int ingenic_set(int pad, const char *func_name, const padmux_io_t *io) {
    const ingenic_soc_t *soc = ingenic_soc();
    if (soc == NULL)
        return IPCHW_PADMUX_NO_TABLE;

    const ingenic_pad_t *pd = find_pad(soc, pad);
    if (pd == NULL)
        return IPCHW_PADMUX_NO_PAD;

    if (!strcmp(func_name, IPCHW_PADMUX_GPIO) || !strcmp(func_name, pd->name)) {
        /* GPIO direction and level are the same nibble as the mux here, so
         * "make it GPIO" has to pick one. A pad that is already GPIO keeps
         * the direction and level it has; one coming off a peripheral becomes
         * an input, which drives nothing onto whatever is soldered there. */
        unsigned code;
        int res = read_code(pad, io, &code);
        if (res != 0)
            return res;
        if (code & FUNC_MSK)
            code &= ~FUNC_INT;
        else
            code = GPIO_INPUT;

        return write_code(pad, code, io);
    }

    for (int f = 0; f < 4; f++) {
        if (strcmp(pd->funcs[f], func_name) != 0)
            continue;
        if (!strcmp(pd->funcs[f], "reserved"))
            break;

        return write_code(pad, (unsigned)f, io);
    }

    return IPCHW_PADMUX_NO_FUNC;
}

const padmux_ops_t PADMUX_OPS_INGENIC = {
    .name = "ingenic",
    .walk = ingenic_walk,
    .get = ingenic_get,
    .set = ingenic_set,
};
