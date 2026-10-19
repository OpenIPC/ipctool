/* Xiongmai XM510 and XM530/XM550: the pad-mux backend.
 *
 * Both parts select a pad's function with one enable bit per alternative,
 * all clear meaning the pad is a GPIO, in the GPIO block at 0x10020000. They
 * differ in where those bits live:
 *
 *   XM530/XM550   one register per pad at 0x10020000 + 4 * pad. Bits 1, 2
 *                 and 3 are MUX1, MUX2 and MUX3 (mach-xm530 platform.h's
 *                 GPIO_MUX1_EN..GPIO_MUX3_EN); the same register carries the
 *                 pad's drive strength and GPIO state, which a mux write has
 *                 to leave alone. Pad N is GPIO N.
 *   XM510         one register per PORT of eight pins at 0x10020000 + 4 *
 *                 port, ports 0..8 (the vendor's GPIOn_MULT_USE_EN words).
 *                 Pin b's three alternatives are bits b, 8 + b and 16 + b;
 *                 pad P * 8 + b is GPIO P_b, the vendor's "gpio81" for 8_1.
 *
 * So in both a pad's alternatives are values of one field in one register,
 * and every row is a plain read-modify-write -- the HiSilicon shape, with a
 * bit per function instead of a selector nibble.
 *
 * Xiongmai publishes no table of what MUX1..3 carry on each pad. The rows are
 * therefore named after the selector -- FUNC1, FUNC2, FUNC3 -- except where
 * the vendor's own code says what it puts there, and those names are its
 * own:
 *
 *   XM530  9..14 MUX2   FLASH_CLK..FLASH_HOLD   platform.h GPIO_FLASH_*,
 *                                               xmsfc_v2.c
 *          43..50, 52   SDIO0_CLK..SDIO0_LENON  sdio0_sd.c sdio0_powerup()
 *            MUX1
 *   XM510  5_0..5_3     SDIO1                   sdio1_sd.c, port 5's word
 *          6_0..6_3     SDIO2                   sdio2_wifi.c, port 6's word
 *
 * XM510's SD and Wi-Fi drivers mux a port's whole word and do not say which
 * pin is clock, command or data, so those pads carry the bus's name. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chipid.h"
#include "hal/xm.h"
#include "padmux.h"

#define XM_GPIO_BASE 0x10020000u

#define XM530_PADS 100
#define XM510_PORTS 9
#define XM510_PADS (XM510_PORTS * 8)

#define XM530_MUX_MASK 0xEu /* MUX1..MUX3, bits 1-3 */

/* A function the vendor names, on one pad and one of its three selectors. */
typedef struct {
    int pad;
    int alt; /* 1..3 */
    const char *name;
} xm_named_t;

static const xm_named_t xm530_named[] = {
    {9, 2, "FLASH_CLK"},     {10, 2, "FLASH_CSN"},   {11, 2, "FLASH_DI"},
    {12, 2, "FLASH_DO"},     {13, 2, "FLASH_WPN"},   {14, 2, "FLASH_HOLD"},
    {43, 1, "SDIO0_CLK"},    {44, 1, "SDIO0_CMD"},   {45, 1, "SDIO0_DATA0"},
    {46, 1, "SDIO0_DATA1"},  {47, 1, "SDIO0_DATA2"}, {48, 1, "SDIO0_DATA3"},
    {49, 1, "SDIO0_DETECT"}, {50, 1, "SDIO0_PWSV"},  {52, 1, "SDIO0_LENON"},
};

static const xm_named_t xm510_named[] = {
    {5 * 8 + 0, 1, "SDIO1"}, {5 * 8 + 1, 1, "SDIO1"}, {5 * 8 + 2, 1, "SDIO1"},
    {5 * 8 + 3, 1, "SDIO1"}, {6 * 8 + 0, 1, "SDIO2"}, {6 * 8 + 1, 1, "SDIO2"},
    {6 * 8 + 2, 1, "SDIO2"}, {6 * 8 + 3, 1, "SDIO2"},
};

static const char *const ALT_NAMES[4] = {NULL, "FUNC1", "FUNC2", "FUNC3"};

/* The pad names, "GPIO43" or "GPIO8_1", as constant data: a row hands out a
 * `const char *` that has to outlive the call, and lookups promise
 * (include/ipchw.h) to read nothing but constant data. */
#define XM_DEC(p)                                                              \
    p "0", p "1", p "2", p "3", p "4", p "5", p "6", p "7", p "8", p "9"
#define XM_PORT(p) p "0", p "1", p "2", p "3", p "4", p "5", p "6", p "7"

static const char xm530_gpio_names[XM530_PADS][7] = {
    XM_DEC("GPIO"),  XM_DEC("GPIO1"), XM_DEC("GPIO2"), XM_DEC("GPIO3"),
    XM_DEC("GPIO4"), XM_DEC("GPIO5"), XM_DEC("GPIO6"), XM_DEC("GPIO7"),
    XM_DEC("GPIO8"), XM_DEC("GPIO9"),
};

static const char xm510_gpio_names[XM510_PADS][8] = {
    XM_PORT("GPIO0_"), XM_PORT("GPIO1_"), XM_PORT("GPIO2_"),
    XM_PORT("GPIO3_"), XM_PORT("GPIO4_"), XM_PORT("GPIO5_"),
    XM_PORT("GPIO6_"), XM_PORT("GPIO7_"), XM_PORT("GPIO8_"),
};

static bool is_xm510(void) { return chip_generation == CHIP_XM510; }

static bool is_xm530(void) { return chip_generation == CHIP_XM530; }

static int npads(void) {
    return is_xm510() ? XM510_PADS : is_xm530() ? XM530_PADS : 0;
}

static const char *gpio_name(int pad) {
    return is_xm510() ? xm510_gpio_names[pad] : xm530_gpio_names[pad];
}

static uint32_t reg_addr(int pad) {
    return XM_GPIO_BASE + 4u * (uint32_t)(is_xm510() ? pad / 8 : pad);
}

/* The field holding this pad's alternatives, in place. */
static uint32_t field_mask(int pad) {
    if (is_xm510()) {
        unsigned b = (unsigned)pad % 8;
        return (1u << b) | (1u << (8 + b)) | (1u << (16 + b));
    }
    return XM530_MUX_MASK;
}

/* Alternative 1..3 of this pad, in place; 0 is GPIO. */
static uint32_t alt_value(int pad, int alt) {
    if (alt == 0)
        return 0;
    if (is_xm510())
        return 1u << ((unsigned)(alt - 1) * 8 + (unsigned)pad % 8);
    return 1u << alt;
}

static const char *alt_name(int pad, int alt) {
    const xm_named_t *t = is_xm510() ? xm510_named : xm530_named;
    size_t n = is_xm510() ? sizeof(xm510_named) / sizeof(*xm510_named)
                          : sizeof(xm530_named) / sizeof(*xm530_named);

    for (size_t i = 0; i < n; i++)
        if (t[i].pad == pad && t[i].alt == alt)
            return t[i].name;
    return ALT_NAMES[alt];
}

static ipchw_padmux_t row(int pad, int alt) {
    ipchw_padmux_t r = {
        .address = reg_addr(pad),
        .func_mask = field_mask(pad),
        .func = (int)alt_value(pad, alt),
        .func_name = alt == 0 ? gpio_name(pad) : alt_name(pad, alt),
        .gpio_name = gpio_name(pad),
        .gpio_pad = pad,
        .gpio_func = 0,
        .flags = IPCHW_PADMUX_F_RMW | IPCHW_PADMUX_F_SHARED_REG |
                 (alt == 0 ? IPCHW_PADMUX_F_GPIO : 0),
    };
    return r;
}

static int xm_walk(padmux_match_fn match, const void *arg, int pad,
                   ipchw_padmux_t *out, int max) {
    int n = npads();
    if (n == 0)
        return IPCHW_PADMUX_NO_TABLE;
    if (pad >= n)
        return 0;

    int found = 0;
    for (int p = pad < 0 ? 0 : pad; p < (pad < 0 ? n : pad + 1); p++) {
        for (int alt = 0; alt <= 3; alt++) {
            ipchw_padmux_t r = row(p, alt);
            if (match != NULL && !match(r.func_name, arg))
                continue;
            if (found < max)
                out[found] = r;
            found++;
        }
    }
    return found;
}

static int xm_get(int pad, ipchw_padmux_t *out, const padmux_io_t *io) {
    int n = npads();
    if (n == 0)
        return IPCHW_PADMUX_NO_TABLE;
    if (pad < 0 || pad >= n)
        return IPCHW_PADMUX_NO_PAD;

    uint32_t v;
    if (!io->read(reg_addr(pad), &v, 32))
        return IPCHW_PADMUX_IO;

    uint32_t sel = v & field_mask(pad);
    for (int alt = 0; alt <= 3; alt++) {
        if (sel == alt_value(pad, alt)) {
            *out = row(pad, alt);
            return 1;
        }
    }
    return 0; /* more than one enable set: nothing this table names */
}

static int xm_set(int pad, const char *func_name, const padmux_io_t *io) {
    int n = npads();
    if (n == 0)
        return IPCHW_PADMUX_NO_TABLE;
    if (pad < 0 || pad >= n)
        return IPCHW_PADMUX_NO_PAD;

    int alt = -1;
    if (!strcmp(func_name, IPCHW_PADMUX_GPIO))
        alt = 0;
    for (int a = 0; a <= 3 && alt < 0; a++)
        if (!strcmp(func_name, row(pad, a).func_name))
            alt = a;
    if (alt < 0)
        return IPCHW_PADMUX_NO_FUNC;

    uint32_t v;
    if (!io->read(reg_addr(pad), &v, 32))
        return IPCHW_PADMUX_IO;
    v = (v & ~field_mask(pad)) | alt_value(pad, alt);
    if (!io->write(reg_addr(pad), v, 32))
        return IPCHW_PADMUX_IO;
    return 0;
}

/* XM530 numbers pads as the vendor does, GPIO N, so the HiSilicon "5_2" is
 * refused there: it would land on pad 42. XM510 is bank-of-eight, which is
 * exactly that spelling. Both also take the vendor's own "GPIO..." form. */
static int xm_parse_pad(const char *spec) {
    int a, b, end = 0;
    const char *s = strncmp(spec, "GPIO", 4) == 0 ? spec + 4 : spec;

    if (is_xm510() && sscanf(s, "%d_%d%n", &a, &b, &end) == 2 &&
        s[end] == '\0') {
        if (a < 0 || a >= XM510_PORTS || b < 0 || b > 7)
            return -1;
        return a * 8 + b;
    }

    char *tail;
    long v = strtol(s, &tail, 10);
    if (tail == s || *tail != '\0' || v < 0 || v >= npads())
        return -1;
    return (int)v;
}

const padmux_ops_t PADMUX_OPS_XM = {
    .name = "xm",
    .walk = xm_walk,
    .get = xm_get,
    .set = xm_set,
    .parse_pad = xm_parse_pad,
};
