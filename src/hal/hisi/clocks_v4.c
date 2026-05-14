/* Clock / PLL / HPM register map for the Hisilicon V4 / Goke V300 family
 * (3516EV200, 3516EV300, 3518EV300, 3516DV200, 7205V200/V210/V300,
 *  7202V300/V330, 7201V200/V300, 7605V100, 7205V500/V510/V530).
 *
 * Same silicon die across HiSilicon and Goke V300 brandings — verified by
 * matching `arch/arm/include/asm/arch-*v300/platform.h` between the two
 * vendor u-boot trees and by benching all three lab boards at the same
 * CPU clock (ipctool#161 ground-truth, 2026-05-14).
 *
 * CPU PLL (APLL) decode borrowed from the Hi3516A SDK kernel patch
 * (`struct hi3516a_pll_clock` in linux-4.9.37.patch):
 *   ctrl_reg1 = CRG_BASE + 0x00: FRACDIV[23:0], POSTDIV1[26:24],
 * POSTDIV2[30:28] ctrl_reg2 = CRG_BASE + 0x04: FBDIV[11:0],   REFDIV[17:12] f =
 * 24 MHz * FBDIV / (REFDIV * POSTDIV1 * POSTDIV2)
 *
 * Validated empirically on V4: read pair (0x12010000, 0x12010004) =
 * (0x12000000, 0x0100104B) decodes to FBDIV=75, REFDIV=1, POSTDIV1=2,
 * POSTDIV2=1 → 900 MHz, within 0.2% of the multi-pattern bench
 * triangulation (see `ipctool cpubench`).
 *
 * NOTE: registers 0x12010014 and 0x1201000c on Goke-branded boards hold
 * FBDIV-shaped values written by the mask ROM based on per-die HPM
 * binning, BUT they do not drive any active clock — empirically confirmed
 * by running identical CPU benchmarks on three V4 boards with three
 * different values at 0x12010014 and finding identical 900 MHz operation.
 * The issue #161 body identifies 0x12010014 as "CPU PLL FBDIV"; that
 * interpretation is falsified. We surface those registers as raw
 * diagnostic dumps so users can still spot per-die HPM correlation
 * without misreading them as clock dividers.
 */

#include "clocks.h"
#include "hal/hisi/hal_hisi.h"

static const struct pll_info v4_plls[] = {
    {
        .name = "cpu_pll",
        .label = "CPU PLL (APLL)",
        .ctrl_reg1 = 0x12010000,
        .frac_shift = 0,
        .frac_width = 24,
        .postdiv1_shift = 24,
        .postdiv1_width = 3,
        .postdiv2_shift = 28,
        .postdiv2_width = 3,
        .ctrl_reg2 = 0x12010004,
        .fbdiv_shift = 0,
        .fbdiv_width = 12,
        .refdiv_shift = 12,
        .refdiv_width = 6,
        .input_khz = 24000,
    },
};

/* CRG[0x80] bits[5:3] — DDR clock mux (see issue #161). */
static const struct mux_entry v4_ddr_table[] = {
    {0b000, 24},
    {0b001, 450},
    {0b011, 300},
    {0b100, 297},
};

static const struct mux_info v4_muxes[] = {
    {
        .name = "ddr",
        .label = "DDR",
        .reg = 0x12010080,
        .sel_shift = 3,
        .sel_mask = 0x07,
        .table = v4_ddr_table,
        .table_len = sizeof(v4_ddr_table) / sizeof(v4_ddr_table[0]),
        .rate_mult =
            4, /* DDR3 quad-pumped — TODO: verify for LPDDRx variants */
    },
};

static const struct hpm_info v4_hpms[] = {
    {
        .name = "hpm",
        .label = "HPM core",
        .reg = 0x1202015C, /* HPM_CHECK_REG */
        .value_shift = 16,
        .value_mask = 0x03FF,
        .window_min = 150,     /* HPM_CORE_MIN */
        .window_max = 350,     /* HPM_CORE_MAX */
        .bin_min = 190,        /* HPM_CORE_VALUE_MIN */
        .bin_max = 310,        /* HPM_CORE_VALUE_MAX */
        .aux_reg = 0x120280D8, /* HPM_CORE_REG0 — per-die fingerprint */
        .aux_name = "hpm_core_reg0",
    },
};

/* Mask-ROM-written HPM-shadow registers — vary by per-die silicon binning,
 * empirically do NOT drive any active clock. Surfaced for diagnostic /
 * fleet-comparison purposes only. See block comment at top of file. */
static const struct raw_reg_info v4_raws[] = {
    {
        .name = "pll_shadow_0c",
        .label = "PLL-shadow @ CRG[0x0c]",
        .reg = 0x1201000C,
        .note = "mask-ROM HPM-bin shadow; not a live FBDIV (see clocks_v4.c)",
    },
    {
        .name = "pll_shadow_14",
        .label = "PLL-shadow @ CRG[0x14]",
        .reg = 0x12010014,
        .note = "mask-ROM HPM-bin shadow; not a live FBDIV (see clocks_v4.c)",
    },
};

const struct clock_family clocks_family_v4 = {
    .chip_id = HISI_V4,
    .label = "Hisilicon V4 / Goke V300",
    .plls = v4_plls,
    .n_plls = sizeof(v4_plls) / sizeof(v4_plls[0]),
    .muxes = v4_muxes,
    .n_muxes = sizeof(v4_muxes) / sizeof(v4_muxes[0]),
    .hpms = v4_hpms,
    .n_hpms = sizeof(v4_hpms) / sizeof(v4_hpms[0]),
    .raws = v4_raws,
    .n_raws = sizeof(v4_raws) / sizeof(v4_raws[0]),
};
