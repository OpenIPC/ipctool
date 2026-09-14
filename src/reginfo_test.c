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
#include "ipchw.h"

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

/* 1333 rows entered by hand from datasheets. These are the invariants the
 * lookups rely on, swept over every table this build carries. */
static void test_table_integrity(void) {
    puts("table sweep: every compiled-in family");

    static const struct {
        int generation;
        const char *name;
    } chips[] = {
        {HISI_V1, "3518EV100"},  {HISI_V2, "3516CV200"},
        {HISI_V2, "3518EV200"},  {HISI_V2A, "3516AV100"},
        {HISI_V3, "3516CV300"},  {HISI_V3A, "3519V101"},
        {HISI_V4, "3516EV200"},  {HISI_V4, "3516EV300"},
        {HISI_V4, "3518EV300"},  {HISI_V4, "3516DV200"},
        {HISI_V4A, "3516CV500"}, {HISI_V4A, "3516AV300"},
        {HISI_OT, "3516CV610"},  {HISI_OT, "3519DV500"},
        {HISI_3536C, "3536CV100"}, {HISI_3536D, "3536DV100"},
    };

    for (size_t c = 0; c < sizeof(chips) / sizeof(*chips); c++) {
        as_chip(chips[c].generation, chips[c].name);

        ipchw_padmux_t rows[2048];
        int n = ipchw_padmux_by_prefix("", rows, 2048);
        if (n == IPCHW_PADMUX_NO_TABLE)
            continue; /* trimmed out of this build */
        if (n <= 0) {
            fprintf(stderr, "  FAIL %s: %d\n", chips[c].name, n);
            failures++;
            continue;
        }
        if (n > 2048)
            n = 2048;

        for (int i = 0; i < n; i++) {
            /* A selector the field cannot hold is a transcription error. */
            if ((uint32_t)rows[i].func > rows[i].func_mask) {
                fprintf(stderr, "  FAIL %s: %s at %#x needs selector %d\n",
                        chips[c].name, rows[i].func_name, rows[i].address,
                        rows[i].func);
                failures++;
            }
            /* A GPIO name that did not parse would silently disable the pad
             * guard in every consumer. */
            if (rows[i].gpio_name != NULL && rows[i].gpio_pad < 0) {
                fprintf(stderr, "  FAIL %s: unparsed GPIO %s at %#x\n",
                        chips[c].name, rows[i].gpio_name, rows[i].address);
                failures++;
            }
        }
    }
}

int main(void) {
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
