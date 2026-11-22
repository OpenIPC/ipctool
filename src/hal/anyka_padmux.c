/* Anyka AK3918AV100 family (AV100, AV100X, EV300L): the pad-mux backend.
 *
 * A pad's function is a three-bit field in one of the SYSCTRL share-pin
 * registers, 0x08000178 + 4 * reg, at a bit offset of the pad's own -- ten
 * pads to a word. Which register and offset, per pad, is the vendor kernel's
 * ak_sharepin[] (drivers/pinctrl/anyka/ak_3918av100_pinctrl.c, pin_set_share:
 * `regval &= ~(0x7 << offset); regval |= sel << offset`), and what each
 * value selects is the device-tree binding header the board trees use
 * (XGPIO_016_FUNC_PWM0 is 2). tools/gen_anyka_padmux.py turns the two into
 * anyka_padmux.h, one table per machine, because the driver's rows differ
 * between CONFIG_MACH_AK3918AV100 and CONFIG_MACH_AK3918EV300L: the EV300L
 * package has 59 of the AV100's 93 muxable pads.
 *
 * The pad number is the GPIO number, as the kernel exports it (pad 16 is
 * /sys/class/gpio/gpio16). GPIO is usually selector 0, but not always: pad 22
 * comes up as CSI0_SCLK with GPIO at 3, pad 98 as SYS_RSTN_OUT with GPIO at
 * 1. The table says which, so the GPIO row is found by name.
 *
 * GPIO37 and GPIO50 are GPIO and nothing else: the driver has no field for
 * them and writes nothing (func_mux_max 1). They are in the table, as rows
 * without IPCHW_PADMUX_F_RMW (no address, func -1), so a lookup knows them
 * and a write of GPIO to them is the no-op it is. GPIO80/81 are the power-management block's and are left out, as the
 * driver leaves them out of the share-pin registers (pin_pmc()).
 *
 * Only the selector is written. Input enable, pull and drive strength are the
 * pad's electrical settings, which the kernel sets from the device tree, not
 * part of choosing what it carries. On the EV300L the MIPI lanes' input
 * enables are in a register of their own (AK_PIN_MIPI_REG); this does not
 * touch them either.
 *
 * WHAT A BOARD RUNS FROM. The serial NOR is on SPI0 (GPIO42..47 as SPI0_*),
 * and the Ethernet MAC is RMII on GPIO0..8, GPIO11 and GPIO23. Driving any
 * of those as a GPIO loses the flash or the network -- on a camera at the
 * end of an SSH session that looks like a hang (OpenIPC/majestic#380). So a
 * pad carrying an SPI0, RMII or OPCLK function is in_use(), and moving it takes
 * ipchw_padmux_set_forced(). */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chipid.h"
#include "hal/anyka.h"
#include "hal/anyka_padmux.h"
#include "padmux.h"

#define AK_PIN_CON0 0x08000178u
#define AK_SEL_MASK 0x7u
#define AK_NO_REG 0xFF

typedef struct {
    const ak_func_t *rows;
    int n;
} ak_table_t;

#define AK_TABLE(t) ((ak_table_t){t, (int)(sizeof(t) / sizeof(*(t)))})

/* The AV100X is an AV100 with more memory; anyka.c reports both as
 * AK39_AV100. The other Anyka generations have other pinctrl drivers and no
 * table here. */
static ak_table_t table(void) {
    switch (chip_generation) {
    case AK39_AV100:
        return AK_TABLE(AK_AV100_funcs);
    case AK39_EV300L:
        return AK_TABLE(AK_EV300L_funcs);
    default:
        return (ak_table_t){NULL, 0};
    }
}

static uint32_t reg_addr(const ak_func_t *f) {
    return f->reg == AK_NO_REG ? IPCHW_PADMUX_ADDR_NONE
                               : AK_PIN_CON0 + 4u * f->reg;
}

/* The rows of the pad row `i` belongs to: [*first, *end). */
static void pad_span(ak_table_t t, int i, int *first, int *end) {
    int a = i, b = i;
    while (a > 0 && t.rows[a - 1].pad == t.rows[i].pad)
        a--;
    while (b < t.n && t.rows[b].pad == t.rows[i].pad)
        b++;
    *first = a;
    *end = b;
}

/* The GPIO row of the pad row `i` belongs to. The generator refuses a pad
 * without one, so this is never -1 on a generated table. */
static int gpio_row_of(ak_table_t t, int i) {
    int first, end;
    pad_span(t, i, &first, &end);
    char want[16];
    snprintf(want, sizeof want, "GPIO%d", t.rows[i].pad);
    for (int k = first; k < end; k++)
        if (!strcmp(padmux_name(t.rows[k].name), want))
            return k;
    return -1;
}

static ipchw_padmux_t row(ak_table_t t, int i) {
    const ak_func_t *f = &t.rows[i];
    const int g = gpio_row_of(t, i);
    const bool fixed = f->reg == AK_NO_REG;
    return (ipchw_padmux_t){
        .address = reg_addr(f),
        .func_mask = fixed ? 0 : AK_SEL_MASK << f->shift,
        .func = fixed ? -1 : (int)((uint32_t)f->sel << f->shift),
        .func_name = padmux_name(f->name),
        .gpio_name = g >= 0 ? padmux_name(t.rows[g].name) : NULL,
        .gpio_pad = f->pad,
        .gpio_func = g < 0 || fixed
                         ? -1
                         : (int)((uint32_t)t.rows[g].sel << f->shift),
        .flags = (fixed ? 0 : IPCHW_PADMUX_F_RMW | IPCHW_PADMUX_F_SHARED_REG) |
                 (g == i ? IPCHW_PADMUX_F_GPIO : 0),
    };
}

/* The first row of pad `pad`, or -1. */
static int find_pad(ak_table_t t, int pad) {
    for (int i = 0; i < t.n; i++)
        if (t.rows[i].pad == pad)
            return i;
    return -1;
}

static int ak_walk(padmux_match_fn match, const void *arg, int pad,
                   ipchw_padmux_t *out, int max) {
    ak_table_t t = table();
    if (t.n == 0)
        return IPCHW_PADMUX_NO_TABLE;

    int found = 0;
    for (int i = 0; i < t.n; i++) {
        if (pad >= 0 && t.rows[i].pad != pad)
            continue;
        ipchw_padmux_t r = row(t, i);
        if (match != NULL && !match(r.func_name, arg))
            continue;
        if (found < max)
            out[found] = r;
        found++;
    }
    return found;
}

static int ak_get(int pad, ipchw_padmux_t *out, const padmux_io_t *io) {
    ak_table_t t = table();
    if (t.n == 0)
        return IPCHW_PADMUX_NO_TABLE;
    int i = find_pad(t, pad);
    if (i < 0)
        return IPCHW_PADMUX_NO_PAD;

    if (t.rows[i].reg == AK_NO_REG) {
        *out = row(t, i); /* GPIO alone; nothing to read */
        return 1;
    }

    uint32_t v;
    if (!io->read(reg_addr(&t.rows[i]), &v, 32))
        return IPCHW_PADMUX_IO;

    const unsigned sel = (v >> t.rows[i].shift) & AK_SEL_MASK;
    int first, end;
    pad_span(t, i, &first, &end);
    for (int k = first; k < end; k++) {
        if (t.rows[k].sel == sel) {
            *out = row(t, k);
            return 1;
        }
    }
    return 0; /* a selector the binding header has no name for */
}

static int ak_set(int pad, const char *func_name, const padmux_io_t *io) {
    ak_table_t t = table();
    if (t.n == 0)
        return IPCHW_PADMUX_NO_TABLE;
    int i = find_pad(t, pad);
    if (i < 0)
        return IPCHW_PADMUX_NO_PAD;

    int first, end, want = -1;
    pad_span(t, i, &first, &end);
    if (!strcmp(func_name, IPCHW_PADMUX_GPIO))
        want = gpio_row_of(t, i);
    for (int k = first; k < end && want < 0; k++)
        if (!strcmp(func_name, padmux_name(t.rows[k].name)))
            want = k;
    if (want < 0)
        return IPCHW_PADMUX_NO_FUNC;

    const ipchw_padmux_t r = row(t, want);
    if (r.address == IPCHW_PADMUX_ADDR_NONE)
        return 0; /* GPIO37/GPIO50: already, and only, GPIO */
    uint32_t v;
    if (!io->read(r.address, &v, 32))
        return IPCHW_PADMUX_IO;
    v = (v & ~r.func_mask) | (uint32_t)r.func;
    if (!io->write(r.address, v, 32))
        return IPCHW_PADMUX_IO;
    return 0;
}

/* The boot flash and the network. Asked only after the front door has read
 * the pad successfully, so a failed read is never reported as one of these. */
static bool ak_in_use(int pad, const padmux_io_t *io) {
    ipchw_padmux_t now;
    if (ak_get(pad, &now, io) != 1)
        return false;
    return !strncmp(now.func_name, "SPI0_", 5) ||
           !strncmp(now.func_name, "RMII_", 5) ||
           !strncmp(now.func_name, "OPCLK", 5);
}

/* "GPIO16" or "16". HiSilicon's "5_2" is refused: Anyka does not number its
 * GPIOs in banks of eight, so it would name a different pad. */
static int ak_parse_pad(const char *spec) {
    const char *s = strncmp(spec, "GPIO", 4) == 0 ? spec + 4 : spec;
    char *tail;
    errno = 0;
    long v = strtol(s, &tail, 10);
    /* Bounded before it is narrowed: "4294967312" is GPIO16 once it has been
     * through an int on a 64-bit host. */
    if (tail == s || *tail != '\0' || errno || v < 0 || v > UINT8_MAX)
        return -1;
    return find_pad(table(), (int)v) >= 0 ? (int)v : -1;
}

/* The number a person types: the selector, as the binding header numbers
 * it and the kernel's pinmux-pins prints it ("function func2"). */
static int ak_func_number(const ipchw_padmux_t *r) {
    if (r->address == IPCHW_PADMUX_ADDR_NONE || r->func_mask == 0)
        return 0;
    unsigned shift = 0;
    while (!((r->func_mask >> shift) & 1u))
        shift++;
    return (int)(((uint32_t)r->func >> shift) & AK_SEL_MASK);
}

const padmux_ops_t PADMUX_OPS_ANYKA = {
    .name = "anyka",
    .walk = ak_walk,
    .get = ak_get,
    .set = ak_set,
    .parse_pad = ak_parse_pad,
    .in_use = ak_in_use,
    .func_number = ak_func_number,
};
