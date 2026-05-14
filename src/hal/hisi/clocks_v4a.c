/* Clock / PLL register map for the Hisilicon V4A family
 * (3516CV500, 3516AV300, 3516DV300 etc. — chip_generation HISI_V4A,
 * 0x3516C500).
 *
 * First-party register map from the V4A mask-ROM reverse engineering at
 * widgetii/HI3516CV500-SDK sdk/bootrom/bootrom-re/regmap-crg.h:
 *
 *   APLL_CONFIG_0 = CRG_BASE + 0x00   (PERI_CRG_PLL0)  CPU PLL
 *   APLL_CONFIG_1 = CRG_BASE + 0x04   (PERI_CRG_PLL1)
 *   DPLL_CONFIG_0 = CRG_BASE + 0x08   (PERI_CRG_PLL2)  DDR PLL
 *   DPLL_CONFIG_1 = CRG_BASE + 0x0C   (PERI_CRG_PLL3)
 *   EPLL_CONFIG_0 = CRG_BASE + 0x10   (PERI_CRG_PLL4)  Ethernet PLL
 *   EPLL_CONFIG_1 = CRG_BASE + 0x14   (PERI_CRG_PLL5)
 *   VPLL_CONFIG_0 = CRG_BASE + 0x18   (PERI_CRG_PLL6)  Video PLL
 *   VPLL_CONFIG_1 = CRG_BASE + 0x1C   (PERI_CRG_PLL7)
 *   CPU_CONFIG    = CRG_BASE + 0x78   (PERI_CRG30)
 *   DDR_SDRAM_CFG = CRG_BASE + 0x7C   (PERI_CRG31)
 *   SOC_FREQ_STAT = CRG_BASE + 0x1E0  (PERI_CRG120)
 *   PLL_LOCK_STAT = CRG_BASE + 0x1E8  (PERI_CRG_PLL122)
 *
 * On V4A all four PLLs share the same bit layout as APLL on V4 (per
 * `struct hi3516a_pll_clock` in the Hi3516A SDK kernel patch):
 *   ctrl_reg1: FRACDIV[23:0], POSTDIV1[26:24], POSTDIV2[30:28]
 *   ctrl_reg2: FBDIV[11:0],   REFDIV[17:12]
 *   f = 24 MHz * FBDIV / (REFDIV * POSTDIV1 * POSTDIV2)
 *
 * Validated on hi3516av300 (chip_generation=HISI_V4A, chip ID
 * SC_CTRL[0xEE0]=0x3516A300):
 *   APLL (0x12000000, 0x0100104B): FBDIV=75 REFDIV=1 PDIV1=2 PDIV2=1 -> 900 MHz
 *   DPLL (0x14000000, 0x01001063): FBDIV=99 REFDIV=1 PDIV1=4 PDIV2=1 -> 594 MHz
 *   EPLL (0x12000000, 0x0000102C): FBDIV=44 REFDIV=1 PDIV1=2 PDIV2=1 -> 528 MHz
 *   VPLL (0x12000000, 0x0100102E): FBDIV=46 REFDIV=1 PDIV1=2 PDIV2=1 -> 552 MHz
 *   PLL_LOCK_STAT = 0x0F: bits 0/1/2/3 all set
 *     bit 0 = APLL, bit 1 = DPLL, bit 3 = EPLL (per V4A bootrom RE check
 *     `& 0xB == 0xB`; bit 2 inferred as VPLL since 0x0F shows all four
 *     PLLs locked and only VPLL is left unaccounted for).
 *
 * HPM characterization on V4A lives in a different subsystem than on V4
 * (HI3516CV500-SDK/sdk/fastburn/fastboot/Source/ddr_training.c):
 *   HPM_CHECK_REG = 0x12020098 (SC_CTRL), sys_hpm_core at bits [24:16]
 *                   (9-bit on V4A, not 10-bit like V4); hpm_core_err at
 *                   bit 26
 *   HPM_CORE_REG0 = 0x120300D8 (MISC), per-die fingerprint
 *   HPM_CORE_REG1 = 0x120300DC (MISC), additional per-die channels
 *   Validity window 150..350 (per the SDK clamp); same nominal binning
 *   range 190..310 used to classify low/mid/high.
 *
 * NOT yet decoded on V4A:
 *   - DDR cksel mux. PERI_CRG31 at 0x7C reads non-zero on the test
 *     board but bit positions for the cksel field aren't in the bootrom
 *     RE and the V4A SDK clock driver doesn't expose DDR as a tunable
 *     Linux clock. Skip until a board with known DDR rate anchors it.
 *
 * Brief survey shows cpu_pll.freq_mhz + hpm.bin. Full `ipctool clocks`
 * additionally shows DPLL/EPLL/VPLL frequencies and lock states.
 */

#include "clocks.h"
#include "hal/hisi/hal_hisi.h"

#define V4A_CRG_BASE 0x12010000
#define V4A_LOCK_REG (V4A_CRG_BASE + 0x1E8)

/* All four PLLs share the Hi3516A APLL bit layout, so only the
 * ctrl_reg* addresses and the lock_bit differ between them. */
#define V4A_PLL_BITS(REG0_OFF, REG1_OFF, LOCK_BIT)                             \
    .ctrl_reg1 = V4A_CRG_BASE + (REG0_OFF), .frac_shift = 0, .frac_width = 24, \
    .postdiv1_shift = 24, .postdiv1_width = 3, .postdiv2_shift = 28,           \
    .postdiv2_width = 3, .ctrl_reg2 = V4A_CRG_BASE + (REG1_OFF),               \
    .fbdiv_shift = 0, .fbdiv_width = 12, .refdiv_shift = 12,                   \
    .refdiv_width = 6, .input_khz = 24000, .lock_reg = V4A_LOCK_REG,           \
    .lock_bit = (LOCK_BIT)

static const struct pll_info v4a_plls[] = {
    {.name = "cpu_pll", .label = "CPU PLL (APLL)", V4A_PLL_BITS(0x00, 0x04, 0)},
    {.name = "ddr_pll", .label = "DDR PLL (DPLL)", V4A_PLL_BITS(0x08, 0x0C, 1)},
    {.name = "eth_pll",
     .label = "Ethernet PLL (EPLL)",
     V4A_PLL_BITS(0x10, 0x14, 3)},
    {.name = "video_pll",
     .label = "Video PLL (VPLL)",
     V4A_PLL_BITS(0x18, 0x1C, 2)},
};

static const struct hpm_info v4a_hpms[] = {
    {
        .name = "hpm",
        .label = "HPM core",
        .reg = 0x12020098, /* HPM_CHECK_REG (SC_CTRL region) */
        .value_shift = 16,
        .value_mask = 0x01FF,  /* 9-bit sys_hpm_core on V4A (vs 10-bit on V4) */
        .window_min = 150,     /* HPM_CORE_MIN (SDK clamp) */
        .window_max = 350,     /* HPM_CORE_MAX */
        .bin_min = 190,        /* nominal binning low */
        .bin_max = 310,        /* nominal binning high */
        .aux_reg = 0x120300D8, /* HPM_CORE_REG0 (MISC region) */
        .aux_name = "hpm_core_reg0",
    },
};

const struct clock_family clocks_family_v4a = {
    .chip_id = HISI_V4A,
    .label = "Hisilicon V4A (CV500 / AV300)",
    .plls = v4a_plls,
    .n_plls = sizeof(v4a_plls) / sizeof(v4a_plls[0]),
    .hpms = v4a_hpms,
    .n_hpms = sizeof(v4a_hpms) / sizeof(v4a_hpms[0]),
};
