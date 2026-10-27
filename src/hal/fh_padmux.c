/* Fullhan FH8852/FH8856/FH8858, V100, V200 and V210: the pad-mux backend.
 *
 * The HiSilicon shape. Every pad has one register in the PMU block, at
 * 0xF0000080 + 4 * pad (arch/arm/mach-fh board.c: fh_pinctrl_init(
 * VA_PMU_REG_BASE + 0x80)), and its function is the four-bit `mfs` field in
 * bits 24..27 of it. The functions each pad offers are the vendor kernel's
 * own iopad.h, turned into fh_padmux.h by tools/gen_fh_padmux.py.
 *
 * TWO NUMBERINGS, and a caller only ever sees one of them. The register is
 * indexed by the vendor's pad id; the kernel exports the pad as the number of
 * its GPIO function -- pad 46 is GPIO9, /sys/class/gpio/GPIO9 -- and that is
 * what every call here takes and what gpio_pad says. A pad with no GPIO
 * function has no number a caller could pass, so it shows up in a walk of
 * the whole table and nowhere else.
 *
 * A WRITE IS FOUR FIELDS, NOT ONE. The kernel's own mux (pinctrl.c,
 * fh_pinctrl_func_select) sets the selector and, for every function, the
 * pad's input enable, output enable and Schmitt trigger: every PINCTRL_FUNC
 * defaults output_enable to 1, and the kernel clears both enables on a pad no
 * device claims at boot (fh_pinctrl_init_pin). A write here does what the
 * kernel's does rather than leave a pad in a state the kernel never would.
 * The rows' func_mask carries all four so a consumer composing its own
 * read-modify-write does the same; get() reads the selector alone, because
 * the enables say nothing about which function is live.
 *
 * Pull and drive strength are left as they are. The vendor sets a default for
 * each function, but they are a pad's electrical setting rather than part of
 * choosing what it carries.
 *
 * THE FLASH SHARES PADS WITH GPIOs. On V200, GPIO7..GPIO10 are also SSI0's
 * TXD, RXD, D2 and D3, and SSI0 is the SPI controller the board boots and
 * runs from. Which of them a board uses depends on its flash -- the vendor's
 * pinctrl has an "SSI0" device and an "SSI0_4BIT" one that also takes D2/D3
 * -- and the hardware does not say: on a Tenda IC7-L the input enable reads
 * set on every pad, used or not, and the output enable reads clear on every
 * pad, even straight after the kernel's own mux set it. So any pad carrying
 * an SSI0 function is treated as the flash's (in_use()), and moving it takes
 * ipchw_padmux_set_forced(): the call for someone who knows the board, such
 * as an owner who has named that pad for something else. That Tenda's
 * GPIO9/GPIO10 are SSI0_D2/D3 unused by its flash, and its IR-cut coils. */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chipid.h"
#include "hal/fh.h"
#include "hal/fh_padmux.h"
#include "padmux.h"

#define FH_PAD_BASE 0xF0000080u

#define FH_SEL_SHIFT 24
#define FH_SEL_MASK (0xFu << FH_SEL_SHIFT) /* mfs */
#define FH_ST (1u << 8)
#define FH_IE (1u << 12)
#define FH_OE (1u << 28)

/* What a mux write sets besides the selector, as fh_pinctrl_func_select()
 * does for every function. */
#define FH_ENABLES (FH_ST | FH_IE | FH_OE)

typedef struct {
    const fh_func_t *rows;
    int n;
} fh_table_t;

#define FH_TABLE(t) ((fh_table_t){t, (int)(sizeof(t) / sizeof(*(t)))})

/* FH8856V201 and FH8858V201 are recognised but have no table: the vendor
 * kernel ships none for them, and their pads are not assumed to match. */
static fh_table_t table(void) {
    switch (chip_generation) {
    case CHIP_FH8852V200:
    case CHIP_FH8856V200:
    case CHIP_FH8858V200:
    case CHIP_FH8852V210:
    case CHIP_FH8856V210:
    case CHIP_FH8858V210:
        return FH_TABLE(FH_V200_funcs);
    case CHIP_FH8852V100:
    case CHIP_FH8856V100:
        return FH_TABLE(FH_V100_funcs);
    default:
        return (fh_table_t){NULL, 0};
    }
}

static uint32_t reg_addr(const fh_func_t *f) {
    return FH_PAD_BASE + 4u * f->pad;
}

/* The rows of the pad row `i` belongs to: [*first, *end). */
static void pad_span(fh_table_t t, int i, int *first, int *end) {
    int a = i, b = i;
    while (a > 0 && t.rows[a - 1].pad == t.rows[i].pad)
        a--;
    while (b < t.n && t.rows[b].pad == t.rows[i].pad)
        b++;
    *first = a;
    *end = b;
}

/* The GPIO row of the pad row `i` belongs to, or -1. */
static int gpio_row_of(fh_table_t t, int i) {
    int first, end;
    pad_span(t, i, &first, &end);
    for (int k = first; k < end; k++) {
        char want[16];
        snprintf(want, sizeof want, "GPIO%d", t.rows[k].gpio);
        if (t.rows[k].gpio >= 0 && !strcmp(padmux_name(t.rows[k].name), want))
            return k;
    }
    return -1;
}

static ipchw_padmux_t row(fh_table_t t, int i) {
    const fh_func_t *f = &t.rows[i];
    const int g = gpio_row_of(t, i);
    return (ipchw_padmux_t){
        .address = reg_addr(f),
        .func_mask = FH_SEL_MASK | FH_ENABLES,
        .func = (int)(((uint32_t)f->sel << FH_SEL_SHIFT) | FH_ENABLES),
        .func_name = padmux_name(f->name),
        .gpio_name = g >= 0 ? padmux_name(t.rows[g].name) : NULL,
        .gpio_pad = g >= 0 ? f->gpio : -1,
        .gpio_func =
            g >= 0 ? (int)(((uint32_t)t.rows[g].sel << FH_SEL_SHIFT) |
                           FH_ENABLES)
                   : -1,
        .flags = IPCHW_PADMUX_F_RMW | IPCHW_PADMUX_F_SHARED_REG |
                 (g == i ? IPCHW_PADMUX_F_GPIO : 0),
    };
}

/* The first row of the pad exported as GPIO `pad`, or -1. */
static int find_pad(fh_table_t t, int pad) {
    for (int i = 0; i < t.n; i++)
        if (t.rows[i].gpio >= 0 && t.rows[i].gpio == pad)
            return i;
    return -1;
}

static int fh_walk(padmux_match_fn match, const void *arg, int pad,
                   ipchw_padmux_t *out, int max) {
    fh_table_t t = table();
    if (t.n == 0)
        return IPCHW_PADMUX_NO_TABLE;

    int found = 0;
    for (int i = 0; i < t.n; i++) {
        if (pad >= 0 && t.rows[i].gpio != pad)
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

static int fh_get(int pad, ipchw_padmux_t *out, const padmux_io_t *io) {
    fh_table_t t = table();
    if (t.n == 0)
        return IPCHW_PADMUX_NO_TABLE;
    int i = find_pad(t, pad);
    if (i < 0)
        return IPCHW_PADMUX_NO_PAD;

    uint32_t v;
    if (!io->read(reg_addr(&t.rows[i]), &v, 32))
        return IPCHW_PADMUX_IO;

    const unsigned sel = (v & FH_SEL_MASK) >> FH_SEL_SHIFT;
    int first, end;
    pad_span(t, i, &first, &end);
    for (int k = first; k < end; k++) {
        if (t.rows[k].sel == sel) {
            *out = row(t, k);
            return 1;
        }
    }
    return 0; /* a selector the vendor's table has no function for */
}

static int fh_set(int pad, const char *func_name, const padmux_io_t *io) {
    fh_table_t t = table();
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
    uint32_t v;
    if (!io->read(r.address, &v, 32))
        return IPCHW_PADMUX_IO;
    v = (v & ~r.func_mask) | (uint32_t)r.func;
    if (!io->write(r.address, v, 32))
        return IPCHW_PADMUX_IO;
    return 0;
}

/* Asked only after the front door has read the pad successfully, so a read
 * that fails is reported as the I/O error it is, never as a flash line. */
static bool fh_in_use(int pad, const padmux_io_t *io) {
    ipchw_padmux_t now;
    return fh_get(pad, &now, io) == 1 && !strncmp(now.func_name, "SSI0_", 5);
}

/* "GPIO9" or "9", both the GPIO number and never the pad id. HiSilicon's
 * "5_2" is refused: Fullhan does not number its GPIOs in banks of eight, so
 * it would name a different pad. */
static int fh_parse_pad(const char *spec) {
    const char *s = strncmp(spec, "GPIO", 4) == 0 ? spec + 4 : spec;
    char *tail;
    errno = 0;
    long v = strtol(s, &tail, 10);
    /* Bounded before it is narrowed: "4294967297" is GPIO1 once it has been
     * through an int on a 64-bit host. A GPIO number is a byte here. */
    if (tail == s || *tail != '\0' || errno || v < 0 || v > INT8_MAX)
        return -1;
    return find_pad(table(), (int)v) >= 0 ? (int)v : -1;
}

/* The number a person types for a function: the selector, as
 * /proc/driver/pinctrl prints it in its `sel` column -- not `func`, which is
 * the whole value a write puts in the register, enables included. */
static int fh_func_number(const ipchw_padmux_t *r) {
    return (int)(((uint32_t)r->func & FH_SEL_MASK) >> FH_SEL_SHIFT);
}

const padmux_ops_t PADMUX_OPS_FH = {
    .name = "fh",
    .walk = fh_walk,
    .get = fh_get,
    .set = fh_set,
    .parse_pad = fh_parse_pad,
    .in_use = fh_in_use,
    .func_number = fh_func_number,
};
