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
#include "hal/sstar.h"
#include "ipchw.h"
#include "padmux.h"

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

/* The row for `func` on a given SoC, when there is exactly one. */
static bool one(const char *func, ipchw_padmux_t *row) {
    int n = ipchw_padmux_by_func(func, row, 1);
    if (n != 1) {
        fprintf(stderr, "  (expected 1 pad for %s, got %d)\n", func, n);
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

    ipchw_padmux_t rows[4];
    int n = ipchw_padmux_by_func("PWM0", rows, 4);
    CHECK(n == 2);
    if (n == 2) {
        CHECK(rows[0].address == 0x200f007c && rows[0].func == 3 &&
              rows[0].gpio_pad == 8);
        CHECK(rows[1].address == 0x200f00e8 && rows[1].func == 2 &&
              rows[1].gpio_pad == 58);
    }

    ipchw_padmux_t r;
    if (one("PWM3", &r)) {
        CHECK(r.address == 0x200f00f4);
        CHECK(r.func == 0);
        CHECK(r.gpio_pad == 61);
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

    /* But that check is only for pads with nothing else to go on. A pad whose
     * alternatives ARE all in the table says GPIO when none of them is
     * asserted, whatever its GPIO-mode bit reads -- an idle pad is assignable,
     * and treating it as unknowable hid 34 of an SSC377D's 86 pads. */
    regs_reset();
    CHECK(ipchw_padmux_get(7, &r) ==
          1); /* PAD_UART1_RX, five modes, none set */
    CHECK((r.flags & IPCHW_PADMUX_F_GPIO) != 0);
    CHECK(r.gpio_name && !strcmp(r.gpio_name, "PAD_UART1_RX"));

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
    ipchw_padmux_t rows[8];
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
        {T31, "T31"},
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
#ifdef IPCHW_PADMUX_V4
    test_v4_pwm();
    test_counts_past_max();
    test_get_set();
#endif
    test_parse_pad();
#ifdef IPCHW_PADMUX_SSTAR
    test_sstar();
#endif
#ifdef IPCHW_PADMUX_INGENIC
    test_ingenic();
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
