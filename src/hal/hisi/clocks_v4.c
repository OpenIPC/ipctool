/* Clock / PLL / HPM register map for the Hisilicon V4 / Goke V300 family
 * (3516EV200, 3516EV300, 3518EV300, 3516DV200, 7205V200/V210/V300,
 *  7202V300/V330, 7201V200/V300, 7605V100, 7205V500/V510/V530).
 *
 * Same silicon die across HiSilicon and Goke V300 brandings -- verified by
 * matching `arch/arm/include/asm/arch-*v300/platform.h` between the two
 * vendor u-boot trees and by benching all three lab boards at the same
 * CPU clock (ipctool#161 ground-truth, 2026-05-14).
 *
 * Register-pair map (per the V4A / HI3516CV500 mask-ROM reverse
 * engineering at widgetii/HI3516CV500-SDK sdk/bootrom/bootrom-re/
 * regmap-crg.h -- V4A is one generation up but shares the CRG layout):
 *
 *   APLL_CONFIG_0 = CRG_BASE + 0x00   CPU PLL
 *   APLL_CONFIG_1 = CRG_BASE + 0x04
 *   DPLL_CONFIG_0 = CRG_BASE + 0x08   DDR PLL
 *   DPLL_CONFIG_1 = CRG_BASE + 0x0C
 *   EPLL_CONFIG_0 = CRG_BASE + 0x10   Ethernet PLL
 *   EPLL_CONFIG_1 = CRG_BASE + 0x14
 *   VPLL_CONFIG_0 = CRG_BASE + 0x18   Video PLL
 *   VPLL_CONFIG_1 = CRG_BASE + 0x1C
 *   PLL_LOCK_STAT = CRG_BASE + 0x1E8  bit 0 = APLL locked
 *                                     (other bits TBD on V4)
 *
 * Only APLL is decoded here; DPLL / EPLL / VPLL FBDIV bit layout differs
 * from APLL (field-shaped bytes sit at bits [23:16] rather than [11:0])
 * and the REFDIV / POSTDIV positions aren't confirmed -- they'd need a
 * DDR-throughput probe and ethernet PHY rate cross-check before being
 * worth shipping.
 *
 * APLL FBDIV decode (CRG_BASE + 0x00 / +0x04), per
 * `struct hi3516a_pll_clock` in Hi3516EV200_SDK_V1.0.1.2's
 * linux-4.9.37.patch -- same struct shape used by V4A bootrom RE for
 * APLL_CONFIG_0/1:
 *   ctrl_reg1 (+0x00): FRACDIV[23:0], POSTDIV1[26:24], POSTDIV2[30:28]
 *   ctrl_reg2 (+0x04): FBDIV[11:0],   REFDIV[17:12]
 *   f = 24 MHz * FBDIV / (REFDIV * POSTDIV1 * POSTDIV2)
 *
 * Validated empirically: read pair (0x12010000, 0x12010004) =
 * (0x12000000, 0x0100104B) decodes to FBDIV=75, REFDIV=1, POSTDIV1=2,
 * POSTDIV2=1 -> 900 MHz, within 0.2% of `ipctool cpubench` multi-pattern
 * triangulation on all three V4 lab boards.
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
        .lock_reg = 0x120101E8, /* PERI_CRG_PLL122 */
        .lock_bit = 0,          /* APLL on V4 */
    },
};

/* CRG[0x80] bits[5:3] -- DDR clock mux (see issue #161). */
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
            4, /* DDR3 quad-pumped -- TODO: verify for LPDDRx variants */
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
        .aux_reg = 0x120280D8, /* HPM_CORE_REG0 -- per-die fingerprint */
        .aux_name = "hpm_core_reg0",
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
};
