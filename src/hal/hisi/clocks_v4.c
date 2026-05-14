/* Clock / PLL / HPM register map for the Hisilicon V4 / Goke V300 family
 * (3516EV200, 3516EV300, 3518EV300, 3516DV200, 7205V200/V210/V300,
 *  7202V300/V330, 7201V200/V300, 7605V100, 7205V500/V510/V530).
 *
 * Same silicon die across HiSilicon and Goke V300 brandings — verified by
 * matching `arch/arm/include/asm/arch-*v300/platform.h` between the two
 * vendor u-boot trees and by benching all three lab boards at the same
 * CPU clock (ipctool#161 ground-truth, 2026-05-14).
 *
 * Register-pair map (first-party from the V4A / HI3516CV500 mask-ROM
 * reverse engineering at
 * github.com/widgetii/HI3516CV500-SDK/blob/master/sdk/bootrom/bootrom-re/
 * regmap-crg.h; V4A is one generation up but shares the CRG layout — the
 * same APLL definition recurs in the Hi3516A SDK kernel patch and is
 * empirically validated on V4 below):
 *
 *   APLL_CONFIG_0 = CRG_BASE + 0x00   (PERI_CRG_PLL0)  CPU PLL
 *   APLL_CONFIG_1 = CRG_BASE + 0x04   (PERI_CRG_PLL1)
 *   DPLL_CONFIG_0 = CRG_BASE + 0x08   (PERI_CRG_PLL2)  DDR PLL
 *   DPLL_CONFIG_1 = CRG_BASE + 0x0C   (PERI_CRG_PLL3)
 *   EPLL_CONFIG_0 = CRG_BASE + 0x10   (PERI_CRG_PLL4)  Ethernet PLL
 *   EPLL_CONFIG_1 = CRG_BASE + 0x14   (PERI_CRG_PLL5)
 *   VPLL_CONFIG_0 = CRG_BASE + 0x18   (PERI_CRG_PLL6)  Video PLL
 *   VPLL_CONFIG_1 = CRG_BASE + 0x1C   (PERI_CRG_PLL7)
 *   PLL_LOCK_STAT = CRG_BASE + 0x1E8  (PERI_CRG_PLL122)
 *       V4A bootrom polls `& 0xB == 0xB` => bit 0 = APLL, bit 1 = DPLL,
 *       bit 3 = EPLL locked (bit 2 likely VPLL; not polled at boot).
 *
 * APLL FBDIV decode (CRG_BASE + 0x00 / +0x04), confirmed against
 * `struct hi3516a_pll_clock` in Hi3516EV200_SDK_V1.0.1.2's
 * linux-4.9.37.patch:
 *   ctrl_reg1 (+0x00): FRACDIV[23:0], POSTDIV1[26:24], POSTDIV2[30:28]
 *   ctrl_reg2 (+0x04): FBDIV[11:0],   REFDIV[17:12]
 *   f = 24 MHz * FBDIV / (REFDIV * POSTDIV1 * POSTDIV2)
 *
 * Validated empirically: read pair (0x12010000, 0x12010004) =
 * (0x12000000, 0x0100104B) decodes to FBDIV=75, REFDIV=1, POSTDIV1=2,
 * POSTDIV2=1 -> 900 MHz, within 0.2% of `ipctool cpubench` multi-pattern
 * triangulation on all three V4 lab boards.
 *
 * DPLL / EPLL / VPLL: register pairs identified per CV500 bootrom RE
 * above, but the FBDIV bit layout DIFFERS from APLL. Field values seen on
 * gk7205v300:
 *   DPLL_CONFIG_1 = 0x018F0000   (issue #161 body decoded `0x8F` -> 1144)
 *   EPLL_CONFIG_1 = 0x01770000   (issue #161 body decoded `0x77` -> 952)
 * If FBDIV lived at bits [11:0] (APLL layout), both would read as 0 and
 * the PLLs would be gated -- but DDR and Ethernet are clearly running.
 * So FBDIV for non-APLL PLLs is at bits [23:16] (8-bit). REFDIV /
 * POSTDIV1 / POSTDIV2 positions: TBD; bench-validate against a DDR
 * throughput probe + ethernet PHY rate read before shipping decoded
 * freq_mhz. For now we just dump the raw config pair so users can spot
 * fleet differences.
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

/* Non-APLL PLL config pairs + lock status. FBDIV decode for these PLLs is
 * TODO (bit layout differs from APLL; see block comment above). For now
 * we just dump raw words so users can correlate with vendor docs and
 * fleet-compare. */
static const struct raw_reg_info v4_raws[] = {
    {
        .name = "ddr_pll",
        .label = "DDR PLL (DPLL)",
        .reg = 0x12010008,  /* DPLL_CONFIG_0 / PERI_CRG_PLL2 */
        .reg2 = 0x1201000C, /* DPLL_CONFIG_1 / PERI_CRG_PLL3 */
        .note = "FBDIV at bits [23:16] (different layout than APLL); "
                "decode TBD -- see clocks_v4.c",
    },
    {
        .name = "eth_pll",
        .label = "Ethernet PLL (EPLL)",
        .reg = 0x12010010,  /* EPLL_CONFIG_0 / PERI_CRG_PLL4 */
        .reg2 = 0x12010014, /* EPLL_CONFIG_1 / PERI_CRG_PLL5 */
        .note = "FBDIV at bits [23:16] (different layout than APLL); "
                "decode TBD -- see clocks_v4.c",
    },
    {
        .name = "video_pll",
        .label = "Video PLL (VPLL)",
        .reg = 0x12010018,  /* VPLL_CONFIG_0 / PERI_CRG_PLL6 */
        .reg2 = 0x1201001C, /* VPLL_CONFIG_1 / PERI_CRG_PLL7 */
        .note = "decode TBD -- see clocks_v4.c",
    },
    {
        .name = "pll_lock_status",
        .label = "PLL lock status",
        .reg = 0x120101E8, /* PERI_CRG_PLL122 */
        .note = "per-bit PLL lock; bit 0 = APLL (CPU). V4A bootrom RE "
                "polls 0xB (bits 0/1/3 for APLL/DPLL/EPLL) at boot, but "
                "V4 typically reads 0x5 (bits 0/2), so the DPLL/EPLL/VPLL "
                "bit assignment on V4 likely differs -- verify per-chip.",
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
