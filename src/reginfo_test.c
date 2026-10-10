/* Pad-mux lookups, exercised on a host with no camera under it.
 *
 * chip_generation and chip_name are the only inputs the lookups have, and
 * both are plain globals, so setting them by hand puts the tables of any SoC
 * in front of the code under test. The values asserted below are the ones a
 * real camera was measured against, so this also pins the tables themselves:
 * an edit that moves PWM1 off 0x100C0010 fails here rather than on a bench. */

#include <stdio.h>
#include <string.h>

#include "chipid.h"
#include "hal/hisi/hal_hisi.h"
#include "hal/ingenic.h"
#include "hal/novatek.h"
#include "hal/novatek_gpio.h"
#include "hal/sstar.h"
#include "hal/sstar_gpio.h"
#include "ipchw.h"
#include "padmux.h"
#include "reginfo.h"

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static void as_chip(int generation, const char *name) {
    chip_generation = generation;
    snprintf(chip_name, sizeof(chip_name), "%s", name);
}

/* The row for `func` on a given SoC, when there is exactly one.
 *
 * Not finding it is a failure, not a reason to skip the caller's assertions:
 * every call site sits inside the #ifdef for that family's table, so the
 * function is always supposed to be there. Returning a quiet false let a
 * whole block of checks evaporate while the run still passed. */
static bool one(const char *func, ipchw_padmux_t *row) {
    int n = ipchw_padmux_by_func(func, row, 1);
    if (n != 1) {
        fprintf(stderr, "  FAIL expected 1 pad for %s, got %d\n", func, n);
        failures++;
        return false;
    }
    return true;
}

/* Each family test only runs where that family's tables were compiled in.
 * IPCHW_PADMUX_* is PUBLIC on the ipchw target precisely so a consumer can ask
 * this question at compile time; asking it here keeps a trimmed build honest
 * instead of red. */
#ifdef IPCHW_PADMUX_V1
static void test_v1_pwm(void) {
    puts("V1 (hi3516cv100): PWM_OUT0 and PWM_OUT1");
    as_chip(HISI_V1, "3518EV100");

    ipchw_padmux_t r;
    if (one("PWM_OUT0", &r)) {
        CHECK(r.address == 0x200f00bc);
        CHECK(r.func == 1);
        CHECK(r.func_mask == 0xf);
        CHECK(r.gpio_name && !strcmp(r.gpio_name, "GPIO5_2"));
        CHECK(r.gpio_pad == 42);
        CHECK(r.gpio_func == 0);
    }
    if (one("PWM_OUT1", &r)) {
        CHECK(r.address == 0x200f00c0);
        CHECK(r.gpio_pad == 43);
    }

    /* The V1 parts spell it PWM_OUT<n>; the later ones do not. A build that
     * only ever asked for "PWM0" would find nothing here and quietly fall
     * back to a plain GPIO lamp. */
    CHECK(ipchw_padmux_by_func("PWM0", &r, 1) == 0);
}
#endif

#ifdef IPCHW_PADMUX_V2
static void test_v2_pwm(void) {
    puts("V2 (hi3518ev200): PWM0 on two pads");
    as_chip(HISI_V2, "3518EV200");

    ipchw_padmux_t rows[16];
    int n = ipchw_padmux_by_func("PWM0", rows, 4);
    CHECK(n == 2);
    if (n == 2) {
        /* muxctrl_reg31 is 000 GPIO1_0, 001 VI_DATA13, 011 I2S_BCLK_TX,
         * 100 PWM0 -- 010 is not assigned, so PWM0 is selector 4 and not the
         * 3 its position in the table would suggest. This assertion said 3
         * until the whole V2 table was rebuilt from the register chapter. */
        CHECK(rows[0].address == 0x200f007c && rows[0].func == 4 &&
              rows[0].gpio_pad == 8);
        /* muxctrl_reg58 has no hole: 00 PMC_PWM, 01 GPIO7_2, 10 PWM0. */
        CHECK(rows[1].address == 0x200f00e8 && rows[1].func == 2 &&
              rows[1].gpio_pad == 58);
    }

    ipchw_padmux_t r;
    if (one("PWM3", &r)) {
        CHECK(r.address == 0x200f00f4);
        CHECK(r.func == 0);
        CHECK(r.gpio_pad == 61);
    }

    puts("V2 (hi3518ev200): SDIO1 sits at selector 4, not 3");
    /* Reported from a camera whose stock firmware drives a WiFi module on
     * SDIO1 (issue #135): every SDIO1 pin wants 0x4, and ipctool said 0x3.
     * The data sheet agrees -- muxctrl_reg4..reg13 each skip 010, so the
     * table had lost a hole and everything past it was off by one. Section
     * 2.4 of the same document is what makes this easy to get wrong: it
     * lists the alternatives in columns headed "Multiplexed Signals 2-4",
     * by POSITION rather than by selector value. */
    static const struct {
        uint32_t address;
        const char *func;
    } sdio1[] = {
        {0x200f0010, "SDIO1_CCLK_OUT"}, {0x200f0014, "SDIO1_CARD_DETECT"},
        {0x200f0018, "SDIO1_CWPR"},     {0x200f001c, "SDIO1_CDATA1"},
        {0x200f0020, "SDIO1_CDATA0"},   {0x200f0024, "SDIO1_CDATA3"},
        {0x200f0028, "SDIO1_CCMD"},     {0x200f002c, "SDIO1_CARD_POWER_EN"},
        {0x200f0034, "SDIO1_CDATA2"},
    };
    for (size_t i = 0; i < sizeof(sdio1) / sizeof(sdio1[0]); i++) {
        if (one(sdio1[i].func, &r)) {
            CHECK(r.address == sdio1[i].address);
            CHECK(r.func == 4);
        }
    }

    /* The hole itself is not a function: nothing answers to it, and it is
     * not offered as an alternative of the pad that carries it. */
    CHECK(ipchw_padmux_by_func("reserved", rows, 16) == 0);
    n = ipchw_padmux_by_pad(16, rows, 16); /* GPIO2_0, muxctrl_reg4 */
    CHECK(n == 4); /* GPIO2_0, RMII_CLK, VO_CLK, SDIO1_CCLK_OUT */
    for (int i = 0; i < n && i < 16; i++)
        CHECK(strcmp(rows[i].func_name, "reserved") != 0);
}
#endif

#ifdef IPCHW_PADMUX_V2A
static void test_v2a_holes(void) {
    puts("V2A (hi3516av100): the two selectors that skip a value");
    as_chip(HISI_V2A, "3516AV100");

    /* Found by diffing all 123 rows against the data sheet's register
     * chapter after issue #135, not by anyone hitting it on a board. Two
     * registers of the Hi3516A/D sheet leave a value out:
     *   muxctrl_reg54  00 GPIO0_1,  10 TEMPER_DQ
     *   muxctrl_reg122 00 GPIO13_7, 01 VI_DAT0, 11 PWM4
     * Both revisions of that document, SPC050 and SPC080, say the same. */
    ipchw_padmux_t rows[8];
    /* Both functions live on several pads here, so ask by address. That is
     * also what made these two visible: TEMPER_DQ sits at selector 2 on
     * muxctrl_reg53, 55, 56 and 57 as well, each of those with a real
     * function at 1 -- so reg54 offering it at 1 was the one row out of step
     * with its neighbours. */
    struct {
        const char *func;
        uint32_t address;
        int selector;
        const char *pad;
    } want[] = {
        {"TEMPER_DQ", 0x200f00d8, 2, "GPIO0_1"},
        {"PWM4", 0x200f01e8, 3, "GPIO13_7"},
        /* Not a hole -- a spelling. The two revisions of the data sheet
         * disagree about this one name: SPC050 calls muxctrl_reg93 value 2
         * RMII_CLK, SPC080 writes out the two signals it is. The table
         * carries the newer one, and the selector is the same either way. */
        {"RMII_CLK_OUT/MII_TX_CLK", 0x200f0174, 2, "GPIO4_0"},
    };
    for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
        int n = ipchw_padmux_by_func(want[i].func, rows,
                                     sizeof(rows) / sizeof(rows[0]));
        CHECK(n > 0);
        bool seen = false;
        for (int k = 0; k < n && k < (int)(sizeof(rows) / sizeof(rows[0]));
             k++) {
            if (rows[k].address != want[i].address)
                continue;
            seen = true;
            CHECK(rows[k].func == want[i].selector);
            CHECK(rows[k].gpio_name && !strcmp(rows[k].gpio_name, want[i].pad));
        }
        CHECK(seen);
    }
}
#endif

#ifdef IPCHW_PADMUX_V4
static void test_v4_pwm(void) {
    puts("V4 (hi3516ev200/ev300): the pads the field already uses");
    as_chip(HISI_V4, "3516EV200");

    ipchw_padmux_t r;
    if (one("PWM1", &r)) {
        CHECK(r.address == 0x100C0010);
        CHECK(r.func == 1);
        CHECK(r.gpio_pad == 4);
    }

    /* PWM3 is on two pads on ev200 and three on ev300, and the one in the
     * field is 0x120C0020. Whichever pad a caller picks, it must pick it
     * deliberately -- there is no single answer to ask for. */
    ipchw_padmux_t rows[8];
    int n = ipchw_padmux_by_func("PWM3", rows, 8);
    CHECK(n == 2);
    bool found_120c0020 = false;
    for (int i = 0; i < n && i < 8; i++) {
        if (rows[i].address == 0x120C0020) {
            found_120c0020 = true;
            CHECK(rows[i].func == 4);
            CHECK(rows[i].gpio_pad == 16);
        }
    }
    CHECK(found_120c0020);

    as_chip(HISI_V4, "3516EV300");
    n = ipchw_padmux_by_func("PWM3", rows, 8);
    CHECK(n == 3);

    /* ev200 and ev300 share one SDK build and one register map but NOT one
     * pad table: pad 72 carries PWM2 on ev300 and nothing on ev200. This is
     * why the pad lookup has to happen at runtime. */
    n = ipchw_padmux_by_pad(72, rows, 8);
    CHECK(n >= 2);
    bool ev300_has_pwm2 = false;
    for (int i = 0; i < n && i < 8; i++)
        if (!strcmp(rows[i].func_name, "PWM2"))
            ev300_has_pwm2 = true;
    CHECK(ev300_has_pwm2);

    as_chip(HISI_V4, "3516EV200");
    n = ipchw_padmux_by_pad(72, rows, 8);
    for (int i = 0; i < n && i < 8; i++)
        CHECK(strcmp(rows[i].func_name, "PWM2") != 0);
}
#endif

#ifdef IPCHW_PADMUX_V4
static void test_v500_pwm(void) {
    puts("V4 (gk7205v510): twelve PWM channels, its own pad table");
    as_chip(HISI_V4, "7205V510");

    /* The IR lamps of the Zenointel SD-2N-4G: PWM8 on GPIO7_0 at selector 5
     * and PWM9 on GPIO6_7 at selector 2, per XMedia's PIN_OUT_V510 and the
     * stock firmware's own pwm.ko. These chips used to get the ev200 table,
     * which calls the first of those pads LCD_DATA4 and knows no PWM8. */
    ipchw_padmux_t r;
    if (one("PWM8", &r)) {
        CHECK(r.address == 0x112C0058);
        CHECK(r.func == 5);
        CHECK(r.gpio_pad == 56);
    }
    if (one("PWM9", &r)) {
        CHECK(r.address == 0x112C0068);
        CHECK(r.func == 2);
        CHECK(r.gpio_pad == 55);
    }
    if (one("PWM11", &r)) {
        CHECK(r.address == 0x112C0074);
        CHECK(r.func == 5);
    }

    ipchw_padmux_t rows[8];
    int n = ipchw_padmux_by_pad(56, rows, 8);
    CHECK(n >= 2);
    for (int i = 0; i < n && i < 8; i++)
        CHECK(strcmp(rows[i].func_name, "LCD_DATA4") != 0);

    /* The core-voltage PWM is its own pad and its own function name, so a
     * PWMn lookup can never land on it. */
    CHECK(ipchw_padmux_by_func("SVB_PWM", rows, 8) == 1);

    /* FMC_STARTUP_DISABLE on UART0_TXD is in the V530 pin-out only. Offering
     * it on a V510 would let a caller write a selector its sheet does not
     * list to the console's TX pad. */
    CHECK(ipchw_padmux_by_func("FMC_STARTUP_DISABLE", rows, 8) == 0);
    as_chip(HISI_V4, "7205V530");
    if (one("FMC_STARTUP_DISABLE", &r)) {
        CHECK(r.address == 0x100C0004);
        CHECK(r.func == 2);
    }
    if (one("PWM8", &r))
        CHECK(r.address == 0x112C0058 && r.func == 5);
}
#endif

#ifdef IPCHW_PADMUX_V1
static void test_prefix_is_not_substring(void) {
    puts("a PWM prefix must not catch SVB_PWM");
    as_chip(HISI_V1, "3518EV100");

    ipchw_padmux_t rows[8];
    int n = ipchw_padmux_by_prefix("PWM", rows, 8);
    CHECK(n == 2); /* PWM_OUT0, PWM_OUT1 -- not the two SVB_PWM pads */
    for (int i = 0; i < n && i < 8; i++)
        CHECK(strncmp(rows[i].func_name, "PWM", 3) == 0);

    /* SVB_PWM is the sensor bias supply, on its own controller. It is in the
     * same table and it ends in the same three letters. */
    CHECK(ipchw_padmux_by_func("SVB_PWM", rows, 8) == 2);
}

static void test_by_pad(void) {
    puts("by pad: the alternatives of one wire, GPIO included");
    as_chip(HISI_V1, "3518EV100");

    ipchw_padmux_t rows[8];
    int n = ipchw_padmux_by_pad(42, rows, 8);
    CHECK(n == 2);
    if (n == 2) {
        CHECK(!strcmp(rows[0].func_name, "GPIO5_2") && rows[0].func == 0);
        CHECK(!strcmp(rows[1].func_name, "PWM_OUT0") && rows[1].func == 1);
        CHECK(rows[0].address == rows[1].address);
    }

    /* A pad the SoC does not have. Not an error -- just nothing. */
    CHECK(ipchw_padmux_by_pad(200, rows, 8) == 0);
}
#endif

#ifdef IPCHW_PADMUX_V4
static void test_counts_past_max(void) {
    puts("the count is matches, not rows written");
    as_chip(HISI_V4, "3516EV300");

    ipchw_padmux_t one_row;
    int n = ipchw_padmux_by_func("PWM3", &one_row, 1);
    CHECK(n == 3);                      /* told the truth */
    CHECK(one_row.func_name != NULL);   /* wrote what it could */

    CHECK(ipchw_padmux_by_func("PWM3", NULL, 0) == 3);
}
#endif

/* The two spellings every gpio subcommand takes, and the near-misses that
 * must not resolve: a mistyped pad is one keystroke away from a register
 * write on a pad somebody else is using. */
static void test_parse_pad(void) {
    puts("a pad number is the whole argument or it is not a pad number");

    CHECK(padmux_parse_pad("42") == 42);
    CHECK(padmux_parse_pad("5_2") == 42);
    CHECK(padmux_parse_pad("0") == 0);
    CHECK(padmux_parse_pad("0_0") == 0);

    CHECK(padmux_parse_pad("5_2junk") == -1);
    CHECK(padmux_parse_pad("1_2_3") == -1);
    CHECK(padmux_parse_pad("42junk") == -1);
    CHECK(padmux_parse_pad("5_8") == -1); /* eight pads to a bank, 0..7 */
    CHECK(padmux_parse_pad("-1") == -1);
    CHECK(padmux_parse_pad("5_-1") == -1);
    CHECK(padmux_parse_pad("") == -1);
    CHECK(padmux_parse_pad(NULL) == -1);
    CHECK(padmux_parse_pad("junk") == -1);
}

static void test_refusals_do_not_exit(void) {
    puts("an unknown SoC is a refusal, not an exit");

    ipchw_padmux_t rows[4];

    as_chip(0, "");
    CHECK(ipchw_padmux_by_func("PWM0", rows, 4) == IPCHW_PADMUX_NO_CHIP);

    as_chip(0x3521, "3521V100"); /* detected by ipctool, but no pad table */
    CHECK(ipchw_padmux_by_func("PWM0", rows, 4) == IPCHW_PADMUX_NO_TABLE);

    /* A family this build trimmed out answers the same way. That is the
     * distinction the caller needs: "you did not compile this in" rather than
     * "this chip has no such pad". */
#ifndef IPCHW_PADMUX_V3
    as_chip(HISI_V3, "3516CV300");
    CHECK(ipchw_padmux_by_func("PWM0", rows, 4) == IPCHW_PADMUX_NO_TABLE);
#endif

    as_chip(HISI_V1, "3518EV100");
    CHECK(ipchw_padmux_by_func(NULL, rows, 4) == IPCHW_PADMUX_BAD_ARG);
    CHECK(ipchw_padmux_by_func("", rows, 4) == IPCHW_PADMUX_BAD_ARG);
    CHECK(ipchw_padmux_by_pad(-1, rows, 4) == IPCHW_PADMUX_BAD_ARG);
    CHECK(ipchw_padmux_by_func("PWM_OUT0", NULL, 4) == IPCHW_PADMUX_BAD_ARG);

    /* Still here. */
    puts("  (process survived every refusal)");
}

/* ------------------------------------------------------------------------
 * A register file made of nothing.
 *
 * ipchw_padmux_get() and ipchw_padmux_set() are the only part of this library
 * that writes to hardware, and a host has no hardware to write to. The io
 * seam in padmux.h exists so the write paths are exercised anyway: seed a few
 * addresses, run the operation, assert on what landed.
 * ---------------------------------------------------------------------- */

static struct {
    uint32_t addr, val;
} REGS[256];
static int NREGS;
static bool IO_FAILS;

/* Which set/clear aliases were poked, so a test can assert that a write went
 * through the alias rather than over the whole register. */
static uint32_t ALIASED[64];
static int NALIASED;

static bool poked(uint32_t addr) {
    for (int i = 0;
         i < NALIASED && i < (int)(sizeof(ALIASED) / sizeof(*ALIASED)); i++)
        if (ALIASED[i] == addr)
            return true;
    return false;
}

static int reg_slot(uint32_t addr) {
    for (int i = 0; i < NREGS; i++)
        if (REGS[i].addr == addr)
            return i;
    return -1;
}

static bool fake_read(uint32_t addr, uint32_t *val, int width) {
    (void)width;
    if (IO_FAILS)
        return false;

    int i = reg_slot(addr);
    *val = i < 0 ? 0 : REGS[i].val;
    return true;
}

/* Ingenic gives every port register a set alias at +4 and a clear alias at
 * +8, and writes only ever go through those -- that is how one pin is changed
 * without reading the other thirty-one. A fake that stored them as plain
 * addresses would make the round trip below assert nothing at all. */
static bool ingenic_alias(uint32_t addr, uint32_t *base, bool *set) {
    if (addr < 0x10010000u || addr >= 0x10018000u)
        return false;

    unsigned off = addr & 0xffu;
    if ((off & 0xfu) != 4 && (off & 0xfu) != 8)
        return false;
    if (off >= 0xf0u) /* PZGID2LD and friends are plain stores */
        return false;

    *set = (off & 0xfu) == 4;
    *base = addr - ((off & 0xfu) == 4 ? 4 : 8);
    return true;
}

static bool fake_write(uint32_t addr, uint32_t val, int width) {
    if (IO_FAILS)
        return false;

    uint32_t base;
    bool set;
    if (ingenic_alias(addr, &base, &set)) {
        uint32_t cur = 0;
        int j = reg_slot(base);
        if (j >= 0)
            cur = REGS[j].val;
        ALIASED[NALIASED++ % (int)(sizeof(ALIASED) / sizeof(*ALIASED))] = addr;
        return fake_write(base, set ? cur | val : cur & ~val, 32);
    }

    int i = reg_slot(addr);
    if (i < 0) {
        if (NREGS == (int)(sizeof(REGS) / sizeof(*REGS))) {
            fprintf(stderr, "  (fake register file full)\n");
            return false;
        }
        i = NREGS++;
        REGS[i].addr = addr;
    }
    REGS[i].val =
        width == 16 ? (REGS[i].val & 0xffff0000u) | (val & 0xffffu) : val;
    return true;
}

static const padmux_io_t FAKE_IO = {fake_read, fake_write};

static void regs_reset(void) {
    NREGS = 0;
    NALIASED = 0;
    IO_FAILS = false;
}

static void seed(uint32_t addr, uint32_t val) {
    regs_reset();
    fake_write(addr, val, 32);
}

static uint32_t reg_of(uint32_t addr) {
    int i = reg_slot(addr);
    return i < 0 ? 0 : REGS[i].val;
}

#ifdef IPCHW_PADMUX_V4
static void test_get_set(void) {
    puts("get/set: the selector moves and nothing else does");
    as_chip(HISI_V4, "3516EV200");

    /* Pad 16 is GPIO2_0 on 0x120C0020, where JTAG_TDI is selector 0, the GPIO
     * is 2 and PWM3 is 4. 0xA30 in the high bits is the drive strength, pull
     * and slew the boot chose: every write here has to leave them alone, and
     * the bug that made this worth a test was a mask of 0xfff0. */
    seed(0x120C0020, 0x00000A30);

    ipchw_padmux_t r;
    CHECK(ipchw_padmux_get(16, &r) == 1);
    CHECK(!strcmp(r.func_name, "JTAG_TDI"));
    CHECK(r.func == 0);
    CHECK(r.gpio_pad == 16);
    CHECK((r.flags & IPCHW_PADMUX_F_RMW) != 0);
    CHECK((r.flags & IPCHW_PADMUX_F_SHARED_REG) != 0);
    CHECK((r.flags & IPCHW_PADMUX_F_GPIO) == 0);

    CHECK(ipchw_padmux_set(16, "PWM3") == 0);
    CHECK(reg_of(0x120C0020) == 0x00000A34);
    CHECK(ipchw_padmux_get(16, &r) == 1);
    CHECK(!strcmp(r.func_name, "PWM3"));

    CHECK(ipchw_padmux_set(16, IPCHW_PADMUX_GPIO) == 0);
    CHECK(reg_of(0x120C0020) == 0x00000A32);
    CHECK(ipchw_padmux_get(16, &r) == 1);
    CHECK((r.flags & IPCHW_PADMUX_F_GPIO) != 0);
    CHECK(!strcmp(r.func_name, "GPIO2_0"));

    /* The pad's own GPIO name means the same thing as IPCHW_PADMUX_GPIO. */
    CHECK(ipchw_padmux_set(16, "PWM3") == 0);
    CHECK(ipchw_padmux_set(16, "GPIO2_0") == 0);
    CHECK(reg_of(0x120C0020) == 0x00000A32);

    /* A selector the table has no name for is not a fabricated row. */
    seed(0x120C0020, 0x0000000F);
    CHECK(ipchw_padmux_get(16, &r) == 0);

    puts("get/set: a refusal writes nothing");
    seed(0x120C0020, 0x00000A30);
    CHECK(ipchw_padmux_set(16, "I2C9_SDA") == IPCHW_PADMUX_NO_FUNC);
    CHECK(reg_of(0x120C0020) == 0x00000A30);
    CHECK(ipchw_padmux_get(4000, &r) == IPCHW_PADMUX_NO_PAD);
    CHECK(ipchw_padmux_set(4000, "PWM3") == IPCHW_PADMUX_NO_PAD);
    CHECK(ipchw_padmux_get(-1, &r) == IPCHW_PADMUX_BAD_ARG);
    CHECK(ipchw_padmux_get(16, NULL) == IPCHW_PADMUX_BAD_ARG);
    CHECK(ipchw_padmux_set(16, NULL) == IPCHW_PADMUX_BAD_ARG);

    /* "reserved" is a hole in the selector, not a function. The walk skips
     * it, so the setter must not resolve it either -- otherwise a caller can
     * put a pad into a state the part does not define. EV200 reg12 has one at
     * selector 5. */
    seed(0x100C0040, 0x00000000);
    CHECK(ipchw_padmux_set(32, "reserved") == IPCHW_PADMUX_NO_FUNC);
    CHECK(reg_of(0x100C0040) == 0x00000000);

    puts("get/set: an unreachable register is said so, not guessed at");
    seed(0x120C0020, 0x00000A30);
    int before = NREGS;
    IO_FAILS = true;
    CHECK(ipchw_padmux_get(16, &r) == IPCHW_PADMUX_IO);
    CHECK(ipchw_padmux_set(16, "PWM3") == IPCHW_PADMUX_IO);
    IO_FAILS = false;
    CHECK(NREGS == before);
    CHECK(reg_of(0x120C0020) == 0x00000A30);
}
#endif

#ifdef IPCHW_PADMUX_SSTAR
static void test_sstar(void) {
    puts("SigmaStar: a mode claims a group of pads, not one");
    as_chip(INFINITY6B, "SSC33X");

    /* reg_fuart_mode is CHIPTOP 0x03[2:0] and its value picks which pads the
     * fast UART lands on. 2 means PAD_GPIO0..3 -- all four of them, one
     * write. This is the shape muxctrl_reg_t cannot hold. */
    ipchw_padmux_t rows[16];
    int n = ipchw_padmux_by_func("FUART_MODE_2", rows, 16);
    CHECK(n == 4);
    for (int i = 0; i < n && i < 4; i++) {
        CHECK(rows[i].address == 0x1F203C0C);
        CHECK(rows[i].func_mask == 0x7);
        CHECK(rows[i].func == 2);
        CHECK(rows[i].gpio_pad == i);
        CHECK((rows[i].flags & IPCHW_PADMUX_F_RMW) != 0);
        /* The pad's other alternatives are in other registers. */
        CHECK((rows[i].flags & IPCHW_PADMUX_F_SHARED_REG) == 0);
        /* And no single value hands the pad back. */
        CHECK(rows[i].gpio_func == -1);
    }

    /* PAD_GPIO0 answers to nine peripherals across five registers, plus the
     * GPIO it has when none of them claims it. */
    n = ipchw_padmux_by_pad(0, rows, 16);
    CHECK(n == 10);
    CHECK((rows[0].flags & IPCHW_PADMUX_F_GPIO) != 0);
    CHECK(rows[0].address == IPCHW_PADMUX_ADDR_NONE);
    CHECK(rows[0].func_mask == 0);
    CHECK(rows[0].gpio_name && !strcmp(rows[0].gpio_name, "PAD_GPIO0"));
    CHECK(rows[0].gpio_pad == 0);

    puts("SigmaStar: dropping a claim leaves the register's other field alone");
    /* PAD_SR_IO01 can be I2C0_MODE_3 at 0x1f203c24[2:0] = 3 or I2C1_MODE_3 at
     * [5:4] = 3 -- the same register, two fields, neither based at bit 0. The
     * 1 in the low field is I2C0 routed to some other pad entirely, and a
     * mask applied at the wrong offset would take it away. */
    seed(0x1F203C24, 0x0031);

    ipchw_padmux_t r;
    CHECK(ipchw_padmux_get(23, &r) == 1);
    CHECK(!strcmp(r.func_name, "I2C1_MODE_3"));
    CHECK(r.func_mask == 0x30 && r.func == 0x30);

    CHECK(ipchw_padmux_set(23, IPCHW_PADMUX_GPIO) == 0);
    CHECK(reg_of(0x1F203C24) == 0x0001);
    CHECK(ipchw_padmux_get(23, &r) == 1);
    CHECK((r.flags & IPCHW_PADMUX_F_GPIO) != 0);

    /* And putting a peripheral back is the one field, not the register. */
    CHECK(ipchw_padmux_set(23, "I2C0_MODE_3") == 0);
    CHECK(reg_of(0x1F203C24) == 0x0003);
    CHECK(ipchw_padmux_get(23, &r) == 1);
    CHECK(!strcmp(r.func_name, "I2C0_MODE_3"));

    CHECK(ipchw_padmux_set(23, "PWM0_MODE_4") == IPCHW_PADMUX_NO_FUNC);
    CHECK(ipchw_padmux_get(4000, &r) == IPCHW_PADMUX_NO_PAD);

    /* PAD_ETH_RN is one of the pads the vendor programs from a hand-written
     * switch rather than from the table, so this build has no claim for it.
     * It must not come back as a free GPIO: a pin page offering the Ethernet
     * pair as wires to drive is worse than one missing four pads. */
    CHECK(ipchw_padmux_by_pad(82, rows, 16) == 0);
    CHECK(ipchw_padmux_get(82, &r) == 0);
    CHECK(ipchw_padmux_set(82, IPCHW_PADMUX_GPIO) == IPCHW_PADMUX_NO_FUNC);

    puts("SigmaStar: get() knows the way back even though the table cannot");
    /* PAD_GPIO0's nine alternatives are fields in five different registers,
     * so no table row can say which value means GPIO. With PWM0_MODE_4 the
     * only thing claiming the pad, clearing that one field is the whole job
     * -- and infinity6b0 has no separate "this pad is GPIO" bit needing a
     * second write -- so the row get() returns carries it. */
    regs_reset();
    CHECK(ipchw_padmux_set(0, "PWM0_MODE_4") == 0);
    CHECK(ipchw_padmux_get(0, &r) == 1);
    CHECK(!strcmp(r.func_name, "PWM0_MODE_4"));
    CHECK(r.address == 0x1F203C1C && r.func_mask == 0x7 && r.func == 4);
    CHECK(r.gpio_func == 0);
    CHECK((r.flags & IPCHW_PADMUX_F_RMW) != 0);

    /* And it is a real write: composing it by hand puts the pad back. */
    fake_write(r.address, (reg_of(r.address) & ~r.func_mask) | r.gpio_func, 16);
    CHECK(ipchw_padmux_get(0, &r) == 1);
    CHECK((r.flags & IPCHW_PADMUX_F_GPIO) != 0);

    /* The lookups still cannot say -- they have not read anything. */
    CHECK(ipchw_padmux_by_func("PWM0_MODE_4", rows, 16) == 1);
    CHECK(rows[0].gpio_func == -1);

    /* Two claims at once and clearing one settles nothing, so neither does
     * the row. PAD_GPIO0 answers to TTL_MODE_1 as well as PWM0_MODE_4. */
    regs_reset();
    CHECK(ipchw_padmux_set(0, "PWM0_MODE_4") == 0);
    fake_write(0x1F203C3C, 0x0040, 16); /* TTL_MODE_1, behind PWM0's back */
    CHECK(ipchw_padmux_get(0, &r) == 1);
    CHECK(r.gpio_func == -1);

    puts("SigmaStar: an unasserted GPIO field is not an idle pad");
    /* infinity6c DOES have rows for its Ethernet pads -- six fields across
     * three banks saying "this pad is GPIO" -- but its ETH_MODE names a
     * different register on each of them, so the generator could not
     * represent it and dropped it. The pad therefore has no mode this build
     * can match, and concluding GPIO from that would report the Ethernet
     * pair as free wire while it is carrying Ethernet. */
    as_chip(INFINITY6C, "SSC37X");

    regs_reset();
    CHECK(ipchw_padmux_set(82, IPCHW_PADMUX_GPIO) == 0);
    CHECK(ipchw_padmux_get(82, &r) == 1);
    CHECK((r.flags & IPCHW_PADMUX_F_GPIO) != 0);
    CHECK(r.gpio_name && !strcmp(r.gpio_name, "PAD_ETH_RN"));

    /* One of the six no longer says GPIO: the honest answer is "cannot say",
     * not "free". */
    fake_write(0x1F2A35C4, 0x0000, 32);
    CHECK(ipchw_padmux_get(82, &r) == 0);

    /* A pad that kept some of its modes but lost one is not complete either.
     * PAD_I2C1_SCL keeps five and lost TEST_IN_MODE_2, whose field the pad
     * still carries as an unnamed claim: asserted, it means something is on
     * the wire that this build can no longer put a name to, and "cannot say"
     * beats offering it as free. */
    regs_reset();
    CHECK(ipchw_padmux_get(58, &r) == 1); /* idle: nothing claims it */
    CHECK((r.flags & IPCHW_PADMUX_F_GPIO) != 0);
    fake_write(0x1F203C48, 0x0002, 16); /* TEST_IN_MODE_2, which we dropped */
    CHECK(ipchw_padmux_get(58, &r) == 0);
    fake_write(0x1F203C48, 0x0000, 16);
    CHECK(ipchw_padmux_get(58, &r) == 1);

    /* But that check is only for pads with nothing else to go on. A pad whose
     * alternatives ARE all in the table says GPIO when none of them is
     * asserted, whatever its GPIO-mode bit reads -- an idle pad is assignable,
     * and treating it as unknowable hid 34 of an SSC377D's 86 pads. */
    regs_reset();
    CHECK(ipchw_padmux_get(7, &r) ==
          1); /* PAD_UART1_RX, five modes, none set */
    CHECK((r.flags & IPCHW_PADMUX_F_GPIO) != 0);
    CHECK(r.gpio_name && !strcmp(r.gpio_name, "PAD_UART1_RX"));

    puts("SigmaStar: a pad the vendor muxes from a bank this table cannot "
         "reach");
    /* Measured on an SSC30KQ. m_stPadMuxTbl gives PAD_ETH_RN..PAD_USB2_DP a
     * row apiece for SPIHOLDN_MODE and EMMC0_8B_MODE_1 -- copy-paste from
     * PAD_SPI_HLD, naming the SPI pad's own fields. The real selector is
     * REG_ETH_GPIO_EN in ALBANY2 (0x1F2A2DC4) for the Ethernet pairs and the
     * UTMI0 power-down bits for USB, neither of which is in this table, so
     * the vendor routes all six through HalPadSetMode_MISC(). Taken at face
     * value the rows report six pads as carrying whatever the flash HOLD pin
     * carries; the board's own answer was "Ethernet", with the link up. */
    as_chip(INFINITY6E, "SSC30KQ");

    regs_reset();
    CHECK(ipchw_padmux_get(120, &r) == 1); /* PAD_SPI_HLD really does */
    CHECK(!strcmp(r.func_name, "SPIHOLDN_MODE"));
    for (int pad = 121; pad <= 126; pad++) {
        CHECK(ipchw_padmux_get(pad, &r) == 0);          /* cannot say */
        CHECK(ipchw_padmux_by_pad(pad, rows, 16) == 0); /* nothing to offer */
        CHECK(ipchw_padmux_set(pad, "EMMC0_8B_MODE_1") == IPCHW_PADMUX_NO_FUNC);
        CHECK(ipchw_padmux_set(pad, IPCHW_PADMUX_GPIO) == IPCHW_PADMUX_NO_FUNC);
    }
    /* and the mode they used to borrow is left to the pad that owns it */
    CHECK(ipchw_padmux_by_func("SPIHOLDN_MODE", rows, 16) == 1);
    CHECK(rows[0].gpio_pad == 120);

    puts("SigmaStar: four PWM fields of one register, three of them claimed");
    /* Also measured on that camera: reg 0x1F207994 read 0x1101, which is
     * PWM0_MODE_1 and PWM2_MODE_1 and PWM3_MODE_1 asserted and PWM1 idle.
     * The negative is the half worth pinning -- a decoder that ignored the
     * field offsets would report all four. */
    regs_reset();
    fake_write(0x1F207994, 0x1101, 16);
    CHECK(ipchw_padmux_get(107, &r) == 1); /* PAD_GPIO8 */
    CHECK(!strcmp(r.func_name, "PWM0_MODE_1"));
    CHECK(ipchw_padmux_get(109, &r) == 1); /* PAD_GPIO10 */
    CHECK(!strcmp(r.func_name, "PWM2_MODE_1"));
    CHECK(ipchw_padmux_get(110, &r) == 1); /* PAD_GPIO11 */
    CHECK(!strcmp(r.func_name, "PWM3_MODE_1"));
    CHECK(ipchw_padmux_get(108, &r) == 1); /* PAD_GPIO9, and PWM1 is idle */
    CHECK(strcmp(r.func_name, "PWM1_MODE_1") != 0);

    as_chip(INFINITY6B, "SSC33X");
}
#endif

#ifdef IPCHW_PADMUX_INGENIC
static void test_ingenic(void) {
    puts("Ingenic: four functions a pad has, and no field that selects them");
    as_chip(T31, "T31");

    /* PB25 is pad 32 + 25. The spec gives it smb1_sda on FUNCTION0 and
     * ssi1_ce0_o on FUNCTION1 -- SMB is what Ingenic calls I2C and SSI is
     * what it calls SPI, which is exactly why func_name is not portable. */
    ipchw_padmux_t rows[16];
    int n = ipchw_padmux_by_pad(57, rows, 8);
    CHECK(n == 3); /* GPIO, SMB1_SDA, SSI1_CE0 -- FUNCTION2 and 3 are unused */
    CHECK((rows[0].flags & IPCHW_PADMUX_F_GPIO) != 0);
    CHECK(rows[0].gpio_name && !strcmp(rows[0].gpio_name, "PB25"));
    if (n >= 2) {
        CHECK(!strcmp(rows[1].func_name, "SMB1_SDA"));
        CHECK(rows[1].func == 0);
        /* Nothing a caller can write: four bits in four registers. */
        CHECK(rows[1].address == IPCHW_PADMUX_ADDR_NONE);
        CHECK(rows[1].func_mask == 0);
        CHECK((rows[1].flags & IPCHW_PADMUX_F_RMW) == 0);
        CHECK(rows[1].gpio_pad == 57);
    }

    /* A pad the package does not bring out is not in the table at all, which
     * is a different answer from a pad whose functions are all unused. */
    ipchw_padmux_t r;
    CHECK(ipchw_padmux_get(44, &r) == IPCHW_PADMUX_NO_PAD); /* PB12 */

    puts("Ingenic: the four bits are staged and committed together");
    regs_reset();
    /* INT 0, MSK 0, PAT1 0, PAT0 1 on bit 25 of port B is FUNCTION1. */
    fake_write(0x10011040, 1u << 25, 32); /* PBPAT0 */
    CHECK(ipchw_padmux_get(57, &r) == 1);
    CHECK(!strcmp(r.func_name, "SSI1_CE0"));

    regs_reset();
    CHECK(ipchw_padmux_set(57, "SMB1_SDA") == 0);
    /* Every write went through a set or clear alias -- one pin's bit, never
     * the other thirty-one -- and the group number committed them. */
    CHECK(poked(0x10011018));       /* PBINTC */
    CHECK(poked(0x10011028));       /* PBMSKC */
    CHECK(poked(0x10011038));       /* PBPAT1C */
    CHECK(poked(0x10011048));       /* PBPAT0C */
    CHECK(reg_of(0x100110F0) == 1); /* PZGID2LD = port B */
    CHECK(ipchw_padmux_get(57, &r) == 1);
    CHECK(!strcmp(r.func_name, "SMB1_SDA"));

    /* The neighbours of the pin are not disturbed. */
    regs_reset();
    fake_write(0x10011030, 0xffffffffu & ~(1u << 25), 32); /* PBPAT1 */
    CHECK(ipchw_padmux_set(57, "SMB1_SDA") == 0);
    CHECK(reg_of(0x10011030) == (0xffffffffu & ~(1u << 25)));

    regs_reset();
    CHECK(ipchw_padmux_set(57, IPCHW_PADMUX_GPIO) == 0);
    CHECK(reg_of(0x10011020) == 1u << 25); /* PBMSK: the GPIO block's */
    CHECK(reg_of(0x10011030) == 1u << 25); /* PBPAT1: an input */
    CHECK(reg_of(0x10011040) == 0);        /* PBPAT0 */
    CHECK(ipchw_padmux_get(57, &r) == 1);
    CHECK((r.flags & IPCHW_PADMUX_F_GPIO) != 0);

    /* A pad already held as a GPIO output keeps the level it was driving:
     * direction and level are the same nibble as the mux here. */
    regs_reset();
    fake_write(0x10011020, 1u << 25, 32); /* PBMSK: already GPIO */
    fake_write(0x10011040, 1u << 25, 32); /* PBPAT0: driving high */
    CHECK(ipchw_padmux_set(57, IPCHW_PADMUX_GPIO) == 0);
    CHECK(reg_of(0x10011040) == 1u << 25); /* still high */
    CHECK(reg_of(0x10011030) == 0);        /* still an output */

    CHECK(ipchw_padmux_set(57, "PWM0") == IPCHW_PADMUX_NO_FUNC);
    CHECK(ipchw_padmux_set(57, "reserved") == IPCHW_PADMUX_NO_FUNC);

    puts("Ingenic: T21 has six ports, and its names come from a board file");
    as_chip(T21, "T21N");

    /* T21 is the one part here with ports past C -- six of them, which its
     * own kernel confirms by registering six gpiochips -- so a pad number
     * reaches 175 and the port arithmetic has to keep up. PF15 is
     * 5 * 32 + 15. */
    n = ipchw_padmux_by_pad(175, rows, 8);
    CHECK(n >= 2);
    if (n >= 2) {
        CHECK(rows[0].gpio_name && !strcmp(rows[0].gpio_name, "PF15"));
        CHECK(rows[0].gpio_pad == 175);
        CHECK((rows[0].flags & IPCHW_PADMUX_F_GPIO) != 0);
        CHECK(!strcmp(rows[1].func_name, "MII_PORTBDF"));
    }

    /* Its names come from the vendor's board file, which spells a device
     * group rather than a wire: the whole DVP bus is one name across fifteen
     * pads. Not the per-wire spelling T31 gets from its GPIO spec, and the
     * only thing that exists for this part. */
    CHECK(ipchw_padmux_by_func("DVP_PORTA", rows, 16) == 15);

    puts("Ingenic: T23 has three, and its board file claims two it has not");
    as_chip(T23, "T23N");

    /* PORT_D and PORT_F appear in T23's board file, left over from a larger
     * part. The port count comes from the SoC's own enum, so nothing on them
     * may reach the table. */
    ipchw_padmux_t r23;
    CHECK(ipchw_padmux_get(96, &r23) == IPCHW_PADMUX_NO_PAD);
    CHECK(ipchw_padmux_get(160, &r23) == IPCHW_PADMUX_NO_PAD);
    CHECK(ipchw_padmux_by_func("DPU_PORTD_SLCD_8BIT", rows, 8) == 0);

    /* The three a live T23 corroborates by itself: the sensor
     * bus, the boot flash and the Ethernet. */
    CHECK(ipchw_padmux_by_func("I2C0_PORTA", rows, 8) == 2);
    if (ipchw_padmux_by_func("I2C0_PORTA", rows, 8) == 2)
        CHECK(rows[0].gpio_pad == 12 && rows[1].gpio_pad == 13);
    CHECK(ipchw_padmux_by_func("SFC_PORTA", rows, 8) == 4);
    CHECK(ipchw_padmux_by_func("GMAC_PORTB", rows, 16) == 10);

    puts("Ingenic: T40 has four ports and comes from a pinctrl devicetree");
    as_chip(T40, "T40");

    /* Four ports, so pad numbers stop at 127. */
    CHECK(ipchw_padmux_get(128, &r23) == IPCHW_PADMUX_NO_PAD);
    CHECK(ipchw_padmux_get(160, &r23) == IPCHW_PADMUX_NO_PAD);

    /* The devicetree labels a routing by device AND port, so the same device
     * on two ports is two names a lookup can tell apart -- the thing a board
     * file's .name could not do. */
    CHECK(ipchw_padmux_by_func("UART0_PC", rows, 8) == 4);
    CHECK(ipchw_padmux_by_func("UART0_PA", rows, 8) == 2);

    /* A group node spanning a range and the per-pin nodes inside it are one
     * device family, not a disagreement: PC02 keeps the numbered name, and
     * the name of the group it came from is gone. */
    CHECK(ipchw_padmux_by_func("PWM0_PC", rows, 8) == 1);
    CHECK(ipchw_padmux_by_func("PWM_PC", rows, 8) == 0);

    as_chip(T31, "T31");
}
#endif

#ifdef IPCHW_PADMUX_NOVATEK
/* The NA51089 TOP block, as the vendor's top_reg.h lays it out. */
#define NVT_REG3 0xF001000Cu  /* SENSOR[2:0] */
#define NVT_REG5 0xF0010014u  /* I2C3[7:6], ETH_MDIO[3:2], ETH[31:30] */
#define NVT_REG9 0xF0010024u  /* UART2[3:2], UART2_CTSRTS[9:8] */
#define NVT_CGPIO 0xF00100A0u /* gate bits: 1 keeps the pad a GPIO */
#define NVT_HGPIO 0xF00100D8u
#define NVT_DSIGPIO 0xF00100E8u

/* Every pad a GPIO and every field 0: what pinmux_init() starts from. */
static void nvt_boot_state(void) {
    regs_reset();
    for (uint32_t r = 0xF00100A0u; r <= 0xF00100E8u; r += 8)
        fake_write(r, 0xFFFFFFFFu, 32);
}

static void test_novatek(void) {
    puts("Novatek: a location field per peripheral and a gate bit per pad");
    as_chip(CHIP_NA51089, "NT98566");

    /* The kernel's own numbers and names; "5_2" is HiSilicon's banking and
     * would land on a different pad. */
    CHECK(padmux_parse_pad("P_GPIO22") == 0x20 + 22);
    CHECK(padmux_parse_pad("MC17") == 17);
    CHECK(padmux_parse_pad("hsi_gpio9") == 0xA0 + 9);
    CHECK(padmux_parse_pad("DSI_GPIO9") == 0xE0 + 9);
    CHECK(padmux_parse_pad("54") == 54);
    CHECK(padmux_parse_pad("5_2") == -1);
    CHECK(padmux_parse_pad("P_GPIO") == -1);
    CHECK(padmux_parse_pad("P_GPIO22x") == -1);

    /* P_GPIO22 is I2C3's SCL on its first location, REG5 I2C3 = 1. */
    ipchw_padmux_t rows[16];
    int n = ipchw_padmux_by_pad(54, rows, 16);
    CHECK(n >= 2);
    CHECK((rows[0].flags & IPCHW_PADMUX_F_GPIO) != 0);
    CHECK(rows[0].gpio_name && !strcmp(rows[0].gpio_name, "P_GPIO22"));
    CHECK(rows[0].address == 0xF00100A8u && rows[0].func_mask == 1u << 22);
    bool found = false;
    for (int i = 1; i < n && i < 16; i++) {
        if (strcmp(rows[i].func_name, "I2C3_1_SCL") != 0)
            continue;
        found = true;
        CHECK(rows[i].address == NVT_REG5);
        CHECK(rows[i].func_mask == 0xC0);
        CHECK(rows[i].func == 0x40);
        /* A field and a gate: never one write. */
        CHECK((rows[i].flags & IPCHW_PADMUX_F_RMW) == 0);
    }
    CHECK(found);

    /* A pad the package does not bond out is not a pad. */
    ipchw_padmux_t r;
    CHECK(ipchw_padmux_get(23, &r) == IPCHW_PADMUX_NO_PAD); /* C_GPIO23 */
    CHECK(ipchw_padmux_get(0x80 + 8, &r) == IPCHW_PADMUX_NO_PAD);

    puts("Novatek: the field, then the gate, and a moved field orphans");
    nvt_boot_state();
    CHECK(ipchw_padmux_get(11, &r) == 1 && (r.flags & IPCHW_PADMUX_F_GPIO));
    /* pinmux_config_i2c(), PIN_I2C_CFG_CH3_2ND_PINMUX: CGPIO_11 and 12 to
     * function, I2C3 = I2C_3_ENUM_I2C_2ND. */
    CHECK(ipchw_padmux_set(11, "I2C3_2_SCL") == 0);
    CHECK((reg_of(NVT_REG5) & 0xC0) == 0x80);
    CHECK(reg_of(NVT_CGPIO) == ~(1u << 11));
    CHECK(ipchw_padmux_get(11, &r) == 1 && !strcmp(r.func_name, "I2C3_2_SCL"));
    /* Only the pad asked about: SDA's gate is its own business. */
    CHECK(ipchw_padmux_get(12, &r) == 1 && (r.flags & IPCHW_PADMUX_F_GPIO));

    /* Moving I2C3 to P_GPIO21 leaves C_GPIO11 handed over to nothing. */
    CHECK(ipchw_padmux_set(0x20 + 21, "I2C3_1_SDA") == 0);
    CHECK((reg_of(NVT_REG5) & 0xC0) == 0x40);
    CHECK(ipchw_padmux_get(11, &r) == 0);

    /* Back to GPIO is the gate bit and nothing else. */
    uint32_t reg5 = reg_of(NVT_REG5);
    CHECK(ipchw_padmux_set(11, IPCHW_PADMUX_GPIO) == 0);
    CHECK(reg_of(NVT_CGPIO) == 0xFFFFFFFFu);
    CHECK(reg_of(NVT_REG5) == reg5);

    puts("Novatek: a modifier needs its location too");
    nvt_boot_state();
    /* pinmux_config_uart(), CH2 | CH2_2ND | CH2_CTSRTS: CGPIO_13 and 14,
     * UART2_CTSRTS = UART_CTSRTS_PINMUX, UART2 = UART2_ENUM_2ND_PINMUX. */
    CHECK(ipchw_padmux_set(13, "UART2_2_CTS") == 0);
    CHECK(reg_of(NVT_REG9) == ((2u << 2) | (1u << 8)));
    CHECK(ipchw_padmux_get(13, &r) == 1 && !strcmp(r.func_name, "UART2_2_CTS"));

    puts("Novatek: parallel sensor data holds through the gate");
    nvt_boot_state();
    fake_write(NVT_REG3, 1, 32); /* SENSOR_ENUM_12BITS_1ST */
    CHECK(ipchw_padmux_get(0xA0 + 4, &r) == 1);
    CHECK(!strcmp(r.func_name, "SENSOR_12BITS"));
    /* MIPI lane: the gate cleared and no field. */
    nvt_boot_state();
    fake_write(NVT_HGPIO, ~(3u << 4), 32);
    CHECK(ipchw_padmux_get(0xA0 + 4, &r) == 1);
    CHECK(!strcmp(r.func_name, "MIPI_LVDS_CLK0"));
    /* GPIO on a parallel data pad has to take the sensor mode down with it:
     * the gate alone does not stop the bus. */
    nvt_boot_state();
    fake_write(NVT_REG3, 1, 32);
    CHECK(ipchw_padmux_set(0xA0 + 4, IPCHW_PADMUX_GPIO) == 0);
    CHECK((reg_of(NVT_REG3) & 7) == 0);
    CHECK(ipchw_padmux_get(0xA0 + 4, &r) == 1 &&
          (r.flags & IPCHW_PADMUX_F_GPIO));

    puts("Novatek: RMII and MDIO overlap on DSI_GPIO9 and RMII wins");
    nvt_boot_state();
    fake_write(NVT_DSIGPIO, ~(1u << 9), 32);
    fake_write(NVT_REG5, (1u << 30) | (1u << 2), 32); /* ETH RMII, ETH_MDIO */
    CHECK(ipchw_padmux_get(0xE0 + 9, &r) == 1 &&
          !strcmp(r.func_name, "ETH_RMII"));
    /* The internal PHY's MDIO there means RMII has to go. */
    CHECK(ipchw_padmux_set(0xE0 + 9, "ETH_MDIO") == 0);
    CHECK(reg_of(NVT_REG5) == (1u << 2));
    CHECK(ipchw_padmux_get(0xE0 + 9, &r) == 1 &&
          !strcmp(r.func_name, "ETH_MDIO"));
    /* And back: of RMII's two claims the one that changes least, leaving
     * ETH_MDIO where it is. */
    CHECK(ipchw_padmux_set(0xE0 + 9, "ETH_RMII") == 0);
    CHECK(reg_of(NVT_REG5) == ((1u << 30) | (1u << 2)));
    CHECK(ipchw_padmux_get(0xE0 + 9, &r) == 1 &&
          !strcmp(r.func_name, "ETH_RMII"));

    CHECK(ipchw_padmux_set(54, "UART2_2_TX") == IPCHW_PADMUX_NO_FUNC);

    puts("Novatek: the primary LCD, from pinmux_select_primary_lcd()");
    nvt_boot_state();
    /* PINMUX_LCD_SEL_PARALLE_RGB565: LCD_TYPE = LCDTYPE_ENUM_PARALLEL_LCD
     * (3) in REG2[3:0], and L_GPIO1..8 handed over for the colour bits. */
    CHECK(ipchw_padmux_set(0x60 + 1, "LCD_PARALLE_RGB565") == 0);
    CHECK((reg_of(0xF0010008u) & 0xF) == 3);
    CHECK(reg_of(0xF00100B8u) == ~(1u << 1));
    CHECK(ipchw_padmux_get(0x60 + 1, &r) == 1 &&
          !strcmp(r.func_name, "LCD_PARALLE_RGB565"));
    as_chip(T31, "T31");
}

/* One driver, two dies: the na51055 driver branches on the chip ID, and the
 * two tables are what it does as each. */
static void test_novatek_na51055_na51084(void) {
    puts("Novatek: NA51055 and NA51084 from one driver");
    ipchw_padmux_t rows[8], r;

    as_chip(CHIP_NA51084, "NT98528");
    CHECK(ipchw_padmux_by_func("I2C4_1_SCL", rows, 8) >= 1);
    CHECK(ipchw_padmux_by_func("TXD2", rows, 8) >= 1); /* RGMII */
    /* No DSI group on these dies. */
    CHECK(ipchw_padmux_get(0xE0 + 9, &r) == IPCHW_PADMUX_NO_PAD);
    /* L_GPIO24 is bonded out here, and not on NA51089. */
    CHECK(ipchw_padmux_by_pad(0x60 + 24, rows, 8) >= 1);

    as_chip(CHIP_NA51055, "NA51055");
    CHECK(ipchw_padmux_by_func("I2C4_1_SCL", rows, 8) == 0);
    CHECK(ipchw_padmux_by_func("SPI3_3_CLK", rows, 8) >= 1);
    CHECK(ipchw_padmux_by_pad(0x60 + 24, rows, 8) >= 1);

    as_chip(CHIP_NA51089, "NT98566");
    CHECK(ipchw_padmux_by_pad(0x60 + 24, rows, 8) == 0); /* not a pad here */
    as_chip(T31, "T31");
}

/* set() from every state the table knows, not only from zeroed registers:
 * each claim's own conditions, with its pad handed over, and from there every
 * function that pad offers. A set that succeeds has to read back; one that
 * refuses has to leave every register as it found it, because a location
 * field moved half-way unmuxes the peripheral from its other pads. */
#include "hal/novatek_padmux.h"

static void novatek_set_from_every_state(int chip, const char *name,
                                         const novatek_soc_t *soc) {
    as_chip(chip, name);
    int done = 0, refused = 0;

    for (int i = 0; i < soc->nclaims; i++) {
        const novatek_claim_t *c = &soc->claims[i];
        for (int j = 0; j < soc->nsels; j++) {
            if (soc->sels[j].pad != c->pad)
                continue;
            const char *want = padmux_name(soc->sels[j].name);

            nvt_boot_state();
            for (int k = 0; k < c->ncond; k++) {
                const novatek_cond_t *f = &soc->conds[c->cond + k];
                uint32_t a = 0xF0010000u + f->reg;
                uint32_t m = ((1u << f->width) - 1) << f->shift;
                fake_write(
                    a, (reg_of(a) & ~m) | ((uint32_t)f->value << f->shift), 32);
            }
            uint32_t gate = 0xF00100A0u + (uint32_t)((c->pad >> 5) * 8);
            if (c->pad >> 5 >= 4)
                gate += 0x10; /* D, H, A, DSI sit after a gap at 0xC0 */
            fake_write(gate, reg_of(gate) & ~(1u << (c->pad & 31)), 32);

            uint32_t before[256];
            int nbefore = NREGS;
            for (int r = 0; r < NREGS; r++)
                before[r] = REGS[r].val;

            int res = ipchw_padmux_set(c->pad, want);
            ipchw_padmux_t got;
            if (res == 0) {
                done++;
                if (ipchw_padmux_get(c->pad, &got) != 1 ||
                    strcmp(got.func_name, want) != 0) {
                    fprintf(stderr, "  FAIL pad %d from %s: set %s reads %s\n",
                            c->pad, padmux_name(c->name), want, got.func_name);
                    failures++;
                }
            } else {
                refused++;
                bool same = true;
                for (int r = 0; r < nbefore; r++)
                    same = same && REGS[r].val == before[r];
                if (!same) {
                    fprintf(stderr,
                            "  FAIL pad %d from %s: refused %s (%d) "
                            "and left registers changed\n",
                            c->pad, padmux_name(c->name), want, res);
                    failures++;
                }
            }
        }
    }
    printf("  %s: %d set, %d refused and undone\n", name, done, refused);
    CHECK(done > 0);
    as_chip(T31, "T31");
}

static void test_novatek_set_from_every_state(void) {
    puts("Novatek: set() from every claim's state reads back or undoes");
    novatek_set_from_every_state(CHIP_NA51089, "NT98566", &NA51089_padmux);
    novatek_set_from_every_state(CHIP_NA51055, "NA51055", &NA51055_padmux);
    novatek_set_from_every_state(CHIP_NA51084, "NT98528", &NA51084_padmux);
}
#endif

#ifdef IPCHW_VENDOR_NOVATEK
/* The controller's four banks, one word per pad group, as the vendor's
 * gpio-nvt-na51089.c addresses them. */
static void test_novatek_gpio_regs(void) {
    puts("Novatek: GPIO controller registers");
    as_chip(CHIP_NA51089, "NT98566");

    CHECK(novatek_gpio_supported());
    CHECK(novatek_gpio_reg(0, NVT_GPIO_DATA) == 0xF0070000u);
    CHECK(novatek_gpio_reg(0x20 + 22, NVT_GPIO_DATA) == 0xF0070004u);
    CHECK(novatek_gpio_reg(0x20 + 22, NVT_GPIO_DIR) == 0xF0070024u);
    CHECK(novatek_gpio_reg(0xE0 + 10, NVT_GPIO_SET) == 0xF007005Cu);
    CHECK(novatek_gpio_reg(0xE0 + 10, NVT_GPIO_CLR) == 0xF007007Cu);
    CHECK(novatek_gpio_valid(22) && !novatek_gpio_valid(23));
    CHECK(novatek_gpio_valid(0x80 + 7) && !novatek_gpio_valid(0x80 + 8));
    CHECK(!novatek_gpio_valid(0xE0 + 11) && !novatek_gpio_valid(-1));
    CHECK(novatek_gpio_reg(23, NVT_GPIO_DATA) == 0);
    CHECK(novatek_gpio_reg(0, 0x10) == 0);

    uint32_t base, len;
    CHECK(novatek_gpio_window(&base, &len));
    CHECK(base == 0xF0070000u && len == 0x80);

    /* NA51055 and NA51084: 13 S, 25 L and 11 D pads, and no DSI. */
    as_chip(CHIP_NA51084, "NT98528");
    CHECK(novatek_gpio_valid(0x60 + 24) && !novatek_gpio_valid(0x60 + 25));
    CHECK(novatek_gpio_valid(0x80 + 10) && !novatek_gpio_valid(0xE0));
    CHECK(novatek_gpio_reg(0x60 + 24, NVT_GPIO_DATA) == 0xF007000Cu);

    as_chip(0, "none");
    CHECK(!novatek_gpio_supported());
    CHECK(novatek_gpio_reg(0, NVT_GPIO_DATA) == 0);
}
#endif

/* Everything the table says a pad can be, put on it and read back.
 *
 * This is the whole self-consistency proof, and the only one the families
 * whose tables are generated from a vendor SDK will ever get on a host: it
 * needs no camera, no datasheet and no SDK, only set() and get() agreeing
 * with the walk over every row of every table this build carries. */
static void test_round_trip(const char *chip, const ipchw_padmux_t *rows,
                            int n) {
    for (int i = 0; i < n; i++) {
        const ipchw_padmux_t *want = &rows[i];
        if (want->gpio_pad < 0)
            continue;

        regs_reset();
        int res = ipchw_padmux_set(want->gpio_pad, want->func_name);
        if (res != 0) {
            fprintf(stderr, "  FAIL %s: set(%d, %s) = %d\n", chip,
                    want->gpio_pad, want->func_name, res);
            failures++;
            continue;
        }

        ipchw_padmux_t got;
        res = ipchw_padmux_get(want->gpio_pad, &got);
        if (res != 1 || strcmp(got.func_name, want->func_name) != 0) {
            fprintf(stderr, "  FAIL %s: pad %d set to %s reads back as %s\n",
                    chip, want->gpio_pad, want->func_name,
                    res == 1 ? got.func_name : "(nothing)");
            failures++;
            continue;
        }

        if (want->gpio_name != NULL) {
            regs_reset();
            if (ipchw_padmux_set(want->gpio_pad, IPCHW_PADMUX_GPIO) != 0 ||
                ipchw_padmux_get(want->gpio_pad, &got) != 1 ||
                !(got.flags & IPCHW_PADMUX_F_GPIO)) {
                fprintf(stderr, "  FAIL %s: pad %d will not go back to GPIO\n",
                        chip, want->gpio_pad);
                failures++;
            }
        }
    }
}

/* 1333 rows entered by hand from datasheets. These are the invariants the
 * lookups rely on, swept over every table this build carries.
 *
 * They are cheap and they are the only guard the generated tables of the
 * non-HiSilicon families will ever get on a host: nothing here needs a camera,
 * a datasheet or a vendor SDK, only the rows themselves agreeing with each
 * other. Every one of them was verified to hold over all sixteen HiSilicon
 * tables before it was written down, so a failure here is a new bug. */
static void check_rows(const char *chip, const ipchw_padmux_t *rows, int n) {
    for (int i = 0; i < n; i++) {
        const ipchw_padmux_t *r = &rows[i];

        /* A selector the field cannot hold is a transcription error. The
         * field is described in place, so this is a bit test rather than a
         * magnitude one: on the families whose selector does not start at bit
         * 0, `func > func_mask` would pass a value sitting in the wrong bits.
         */
        if (r->func >= 0 && r->func_mask != 0 &&
            ((uint32_t)r->func & ~r->func_mask) != 0) {
            fprintf(stderr, "  FAIL %s: %s at %#x needs selector %#x of %#x\n",
                    chip, r->func_name, r->address, (unsigned)r->func,
                    r->func_mask);
            failures++;
        }

        /* A GPIO name that did not parse would silently disable the pad guard
         * in every consumer. */
        if (r->gpio_name != NULL && r->gpio_pad < 0) {
            fprintf(stderr, "  FAIL %s: unparsed GPIO %s at %#x\n", chip,
                    r->gpio_name, r->address);
            failures++;
        }

        if (r->func_name == NULL || !*r->func_name) {
            fprintf(stderr, "  FAIL %s: nameless row at %#x\n", chip,
                    r->address);
            failures++;
        }
    }

    /* Everything below is about one pad at a time. The walk emits a pad's rows
     * consecutively, so a linear pass finds the groups. */
    for (int i = 0; i < n;) {
        int pad = rows[i].gpio_pad;
        int j = i;
        while (j < n && rows[j].gpio_pad == pad)
            j++;
        if (pad < 0) {
            i = j;
            continue;
        }

        for (int a = i; a < j; a++) {
            /* One pad, one spelling of its GPIO. */
            if (rows[a].gpio_name == NULL ||
                strcmp(rows[a].gpio_name, rows[i].gpio_name) != 0) {
                fprintf(stderr, "  FAIL %s: pad %d is both %s and %s\n", chip,
                        pad, rows[i].gpio_name,
                        rows[a].gpio_name ? rows[a].gpio_name : "(null)");
                failures++;
            }
            for (int b = a + 1; b < j; b++) {
                /* Two rows of one pad with the same name make set() ambiguous
                 * and row_for() arbitrary in every consumer. */
                if (!strcmp(rows[a].func_name, rows[b].func_name)) {
                    fprintf(stderr, "  FAIL %s: pad %d offers %s twice\n", chip,
                            pad, rows[a].func_name);
                    failures++;
                }
                /* Two functions of one pad selected by the same value of the
                 * same field cannot both be reached. */
                if (rows[a].address == rows[b].address &&
                    rows[a].func_mask == rows[b].func_mask &&
                    rows[a].func == rows[b].func) {
                    fprintf(stderr,
                            "  FAIL %s: pad %d selects %s and %s alike\n", chip,
                            pad, rows[a].func_name, rows[b].func_name);
                    failures++;
                }
            }
        }
        i = j;
    }
}

/* Two pads must never answer to one number: ipchw_padmux_by_pad() would hand
 * back the alternatives of a wire the caller did not ask about. */
static void check_pads_unique(const char *chip, const ipchw_padmux_t *rows,
                              int n) {
    for (int i = 0; i < n;) {
        int pad = rows[i].gpio_pad;
        int j = i;
        while (j < n && rows[j].gpio_pad == pad)
            j++;
        if (pad >= 0) {
            for (int k = j; k < n; k++) {
                if (rows[k].gpio_pad == pad) {
                    fprintf(stderr, "  FAIL %s: pad %d is in two places\n",
                            chip, pad);
                    failures++;
                    break;
                }
            }
        }
        i = j;
    }
}

static void test_table_integrity(void) {
    puts("table sweep: every compiled-in family");

    static const struct {
        int generation;
        const char *name;
    } chips[] = {
        {HISI_V1, "3518EV100"},
        {HISI_V2, "3516CV200"},
        {HISI_V2, "3518EV200"},
        {HISI_V2A, "3516AV100"},
        {HISI_V3, "3516CV300"},
        {HISI_V3A, "3519V101"},
        {HISI_V4, "3516EV200"},
        {HISI_V4, "3516EV300"},
        {HISI_V4, "3518EV300"},
        {HISI_V4, "3516DV200"},
        {HISI_V4, "7205V510"},
        {HISI_V4, "7205V530"},
        {HISI_V4A, "3516CV500"},
        {HISI_V4A, "3516AV300"},
        {HISI_OT, "3516CV610"},
        {HISI_OT, "3519DV500"},
        {HISI_3536C, "3536CV100"},
        {HISI_3536D, "3536DV100"},
#ifdef IPCHW_PADMUX_SSTAR
        {INFINITY6, "SSC32X"},
        {INFINITY6B, "SSC33X"},
        {INFINITY6E, "SSC33X"},
        {INFINITY6C, "SSC37X"},
#endif
#ifdef IPCHW_PADMUX_INGENIC
        {T21, "T21N"},
        {T23, "T23N"},
        {T31, "T31"},
        {T40, "T40"},
#endif
#ifdef IPCHW_PADMUX_NOVATEK
        {CHIP_NA51089, "NT98566"},
        {CHIP_NA51055, "NA51055"},
        {CHIP_NA51084, "NT98528"},
#endif
    };

    for (size_t c = 0; c < sizeof(chips) / sizeof(*chips); c++) {
        as_chip(chips[c].generation, chips[c].name);

        static ipchw_padmux_t rows[4096];
        int n = ipchw_padmux_by_prefix("", rows, 4096);
        if (n == IPCHW_PADMUX_NO_TABLE)
            continue; /* trimmed out of this build */
        if (n <= 0) {
            fprintf(stderr, "  FAIL %s: %d\n", chips[c].name, n);
            failures++;
            continue;
        }
        if (n > 4096) {
            fprintf(stderr, "  FAIL %s: %d rows, buffer holds 4096\n",
                    chips[c].name, n);
            failures++;
            n = 4096;
        }

        check_rows(chips[c].name, rows, n);
        check_pads_unique(chips[c].name, rows, n);
        test_round_trip(chips[c].name, rows, n);
    }
}

/* The per-pad register addresses of the Infinity6C GPIO block, as measured on
 * a live board: pads 12 and 30 move their bytes at exactly these addresses
 * when written through sysfs, and the idle levels of 10, 23 and 40/41 read
 * back what their exporters left. This pins the table an address edit would
 * silently move. */
#ifdef IPCHW_VENDOR_SSTAR
static void test_sstar_gpio_regs(void) {
    puts("SigmaStar: per-pad GPIO registers (Infinity6C)");
    as_chip(INFINITY6C, "SSC37X");

    CHECK(sstar_gpio_supported());
    CHECK(sstar_gpio_num_pads() == 82);
    CHECK(sstar_gpio_pad_addr(0) == 0x1F207C00);
    CHECK(sstar_gpio_pad_addr(12) == 0x1F207C30);
    CHECK(sstar_gpio_pad_addr(23) == 0x1F207C5C);
    CHECK(sstar_gpio_pad_addr(30) == 0x1F207C7C);
    CHECK(sstar_gpio_pad_addr(41) == 0x1F207CA8);
    CHECK(sstar_gpio_pad_addr(42) == 0x1F207CC4);
    CHECK(sstar_gpio_pad_addr(81) == 0x1F207D60);

    uint32_t prev = 0;
    for (int pad = 0; pad < 82; pad++) {
        uint32_t addr = sstar_gpio_pad_addr(pad);
        CHECK(addr >= 0x1F207C00 && addr <= 0x1F207D60);
        CHECK((addr & 3) == 0);
        CHECK(addr > prev);
        prev = addr;
    }

    /* The window the IR-cut hint asks the /proc walk about -- the call
     * itself, not a stand-in for it: one page-aligned start, an end that is
     * not. */
    uint32_t base, len;
    CHECK(sstar_gpio_window(&base, &len));
    CHECK(base == 0x1F207000 && len == 0xD64);
    CHECK(gpio_windows_in_mapping(0x1F000000, 0x400000, base, len, 1) == 0x1);
    CHECK(gpio_windows_in_mapping(0x1F200000, 0x10000, base, len, 1) == 0x1);
    CHECK(gpio_windows_in_mapping(0x1F207000, 0x1000, base, len, 1) == 0x1);
    CHECK(gpio_windows_in_mapping(0x1F206000, 0x1000, base, len, 1) == 0x0);
    CHECK(gpio_windows_in_mapping(0x1F208000, 0x1000, base, len, 1) == 0x0);

    as_chip(0, "none");
    CHECK(!sstar_gpio_supported());
    CHECK(sstar_gpio_num_pads() == 0);
    CHECK(sstar_gpio_pad_addr(31) == 0);
    CHECK(!sstar_gpio_window(&base, &len));
}
#endif

/* parse_gpio_level: 0 is a level the command really writes, so the string
 * must be exactly the level, and every string strtoul reads as 0 is a
 * refusal. The old bare strtoul made `gpio set 12 foo` drive the pad low. */
static void test_gpio_level_parse(void) {
    puts("gpio set: a level is the string, and only 0 or 1 is a level");
    unsigned level;

    CHECK(parse_gpio_level("0", &level) && level == 0);
    CHECK(parse_gpio_level("1", &level) && level == 1);
    CHECK(parse_gpio_level("01", &level) && level == 1);

    CHECK(!parse_gpio_level("", &level));
    CHECK(!parse_gpio_level("foo", &level));
    CHECK(!parse_gpio_level("1x", &level));
    CHECK(!parse_gpio_level("0x1", &level));
    CHECK(!parse_gpio_level("2", &level));
    CHECK(!parse_gpio_level("-1", &level));
    CHECK(!parse_gpio_level("18446744073709551616", &level));
}

/* gpio_windows_in_mapping: a mapping covers what its length covers, not
 * only what its start address falls inside -- a daemon holding one broad
 * window from below the registers is holding them the same as one that
 * mapped the exact page. */
static void test_gpio_windows_in_mapping(void) {
    puts("gpio reports: a mapping covers what its length covers");
    uint32_t base = 0x1000, stride = 0x100;

    CHECK(gpio_windows_in_mapping(0x1000, 0x100, base, stride, 3) == 0x1);
    CHECK(gpio_windows_in_mapping(0x0, 0x1200, base, stride, 3) == 0x3);
    CHECK(gpio_windows_in_mapping(0x1100, 0x200, base, stride, 3) == 0x6);
    CHECK(gpio_windows_in_mapping(0x1300, 0x100, base, stride, 3) == 0x0);
    CHECK(gpio_windows_in_mapping(0x1250, 0x20, base, stride, 3) == 0x4);
}

int main(void) {
    /* Every register the tests below touch is one of these, not a camera's. */
    padmux_set_io(&FAKE_IO);

#ifdef IPCHW_PADMUX_V1
    test_v1_pwm();
    test_prefix_is_not_substring();
    test_by_pad();
#endif
#ifdef IPCHW_PADMUX_V2
    test_v2_pwm();
#endif
#ifdef IPCHW_PADMUX_V2A
    test_v2a_holes();
#endif
#ifdef IPCHW_PADMUX_V4
    test_v4_pwm();
    test_v500_pwm();
    test_counts_past_max();
    test_get_set();
#endif
    test_parse_pad();
#ifdef IPCHW_PADMUX_SSTAR
    test_sstar();
#endif
#ifdef IPCHW_VENDOR_SSTAR
    test_sstar_gpio_regs();
#endif
    test_gpio_level_parse();
    test_gpio_windows_in_mapping();
#ifdef IPCHW_PADMUX_INGENIC
    test_ingenic();
#endif
#ifdef IPCHW_PADMUX_NOVATEK
    test_novatek();
    test_novatek_na51055_na51084();
    test_novatek_set_from_every_state();
#endif
#ifdef IPCHW_VENDOR_NOVATEK
    test_novatek_gpio_regs();
#endif
    test_refusals_do_not_exit();
    test_table_integrity();

    if (failures) {
        fprintf(stderr, "\n%d check(s) failed\n", failures);
        return 1;
    }
    puts("\nall pad-mux checks passed");
    return 0;
}
