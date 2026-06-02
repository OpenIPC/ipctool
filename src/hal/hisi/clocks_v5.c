/* Clock / PLL register map for the Hisilicon V5 / HISI_OT family
 * (Hi3516CV610 / Hi3516CV613 / Hi3516DV500 / Hi3519DV500).
 *
 * CRG layout (per Hi3516CV610_SDK_V1.0.2.0
 *   smp/a7_linux/source/bsp/components/gsl/include/platform.h
 *   and .../drivers/secure_driver/include/platform.h):
 *
 *     REG_BASE_CRG = 0x11010000
 *     PERI_CRG_PLL0  = 0x000     APLL_CONFIG_0  (CPU PLL)
 *     PERI_CRG_PLL1  = 0x004     APLL_CONFIG_1
 *     APLL_LOCK_REG  = 0x038
 *     VPLL_CONFIG_0  = 0x080     (Video PLL)
 *     VPLL_CONFIG_1  = 0x084
 *     VPLL_LOCK_REG  = 0x0B8     (sys_ctrl.c "wait vpll effect")
 *     DPLL_CONFIG_0  = 0x180     (DDR PLL)
 *     DPLL_CONFIG_1  = 0x184
 *     DPLL_LOCK_REG  = 0x1B8
 *     EPLL_CONFIG_0  = 0x200     (Ethernet PLL)
 *     EPLL_CONFIG_1  = 0x204     (PERI_CRG_PLL129)
 *     EPLL_LOCK_REG  = 0x238
 *
 * Lock register layout (per `apll_lock_status` union in
 *   smp/a7_linux/source/bsp/components/gsl/boot/reset.c):
 *     bit 0 = apll_lock       (instantaneous)
 *     bit 4 = apll_lock_final (stable -- the bit we report)
 *
 * PLL field layout: identical to V4 (`struct hi3516a_pll_clock`):
 *     ctrl_reg1 (+0x00): FRACDIV[23:0], POSTDIV1[26:24], POSTDIV2[30:28]
 *     ctrl_reg2 (+0x04): FBDIV[11:0],   REFDIV[17:12]
 *     f = 24 MHz * (FBDIV + FRACDIV/2^24) / (REFDIV * POSTDIV1 * POSTDIV2)
 *
 * Ground-truthed on Hi3519DV500 demo board (OS08A10 sensor):
 *   APLL @ 0x11010000=0x12800000, 0x11010004=0x103e -> 750.000 MHz (CPU)
 *   VPLL @ 0x11010080=0x1289374b, 0x11010084=0x1041 -> 786.432 MHz
 *   DPLL @ 0x11010180=0x12555555, 0x11010184=0x1037 -> 664.000 MHz (DDR)
 *   EPLL @ 0x11010200=0x12155555, 0x11010204=0x1034 -> 625.000 MHz (ETH)
 *
 * Beyond the four PLLs this file also decodes (via v5_extra):
 *   - DDR data rate: DPLL post-divided * (1 << dficlk_ratio) where
 *     dficlk_ratio lives in DDR_PHY0+0x78 bits[1:0]; ddr_cksel at
 *     CRG+0x2000 bits[18:16] selects between 24 MHz and clk_dpll_pst
 *   - DRAM type from DDR_PHY0+0x2C bits[3:0]
 *   - SSMOD (DPLL spread-spectrum) at CRG+0x190 bits[0],[2]
 *   - per-site live HPM samples (NPU / MDA / CORE) at HPM_BASE 0x1102B000
 * The canonical per-die HPM binning value (HPM_STORAGE_REG @ SYSCTRL+0x340
 * bits[9:0]) is surfaced through the standard hpm_info table.
 */

#include "cjson/cJSON.h"
#include "clocks.h"
#include "hal/hisi/hal_hisi.h"
#include "tools.h"

#define V5_CRG_BASE 0x11010000u
/* DDR PHY0 base (DDR_REG_BASE_PHY0 in vendor SDK). */
#define V5_DDR_PHY0_BASE 0x11150000u
/* SYSCTRL base (REG_SYSCTRL_BASE / DDR_REG_BASE_SYSCTRL in vendor SDK). */
#define V5_SYSCTRL_BASE 0x11020000u
/* HPM register block (HPM_BASE_ADDR in svb.h). */
#define V5_HPM_BASE 0x1102B000u

/* DPLL spread-spectrum modulation control (CRG+0x190).
 *   bit[0] = ssmod_cken
 *   bit[2] = ssmod_disable  (1 = SSMOD off)
 * Definitions from the V5 u-boot patch at
 *   open_source/u-boot/u-boot-2022.07.patch around line 30246. */
#define V5_SSMOD_CTRL_OFF 0x190

#define V5_PLL(name_str, label_str, off_ctrl1, off_lock)                       \
    {                                                                          \
        .name = (name_str),                                                    \
        .label = (label_str),                                                  \
        .ctrl_reg1 = V5_CRG_BASE + (off_ctrl1),                                \
        .frac_shift = 0,                                                       \
        .frac_width = 24,                                                      \
        .postdiv1_shift = 24,                                                  \
        .postdiv1_width = 3,                                                   \
        .postdiv2_shift = 28,                                                  \
        .postdiv2_width = 3,                                                   \
        .ctrl_reg2 = V5_CRG_BASE + (off_ctrl1) + 4,                            \
        .fbdiv_shift = 0,                                                      \
        .fbdiv_width = 12,                                                     \
        .refdiv_shift = 12,                                                    \
        .refdiv_width = 6,                                                     \
        .input_khz = 24000,                                                    \
        .lock_reg = V5_CRG_BASE + (off_lock),                                  \
        .lock_bit = 4, /* apll_lock_final */                                   \
    }

static const struct pll_info v5_plls[] = {
    V5_PLL("cpu_pll", "CPU PLL (APLL)", 0x000, 0x038),
    V5_PLL("video_pll", "Video PLL (VPLL)", 0x080, 0x0B8),
    V5_PLL("ddr_pll", "DDR PLL (DPLL)", 0x180, 0x1B8),
    V5_PLL("eth_pll", "Ethernet PLL (EPLL)", 0x200, 0x238),
};

/* Per-die HPM (Hisilicon Process Monitor) binning value.
 *
 * Three HPM measurement sites exist on V5 (NPU at HPM_BASE+0x18/+0x1C,
 * MDA at +0x28/+0x2C, CORE at +0x38/+0x3C), each holding two 10-bit
 * readings per register (u_hpm_reg layout: bits[9:0]=hpm0,
 * bits[25:16]=hpm1). The instantaneous CORE readings are emitted as a
 * per-site fingerprint by v5_extra().
 *
 * The canonical value mapped here is HPM_STORAGE_REG @ SYSCTRL+0x340
 * (u_hpm_storage_reg.core_hpm_value, bits[9:0]) -- this is the binning
 * value boot software writes once it has averaged the CORE readings.
 *
 * Binning thresholds from svb.h:
 *   CORE_HPM_BOUND_20      = 222   (SVB_20 cut-off)
 *   CORE_HPM_BOUND_10      = 230   (SVB_10 cut-off)
 *   CORE_HPM_BOUND_10_ESMT = 210
 * The exact bound chosen depends on the SVB version (SVB_VER_REG bits[5:2]).
 * Here we use a generic window 210..310 and let clocks.c's hpm_bin()
 * tri-state it; the precise SVB-version-aware threshold can be layered on
 * top by anyone who needs the manufacturing meaning. */
static const struct hpm_info v5_hpms[] = {
    {
        .name = "hpm",
        .label = "HPM core (per-die)",
        .reg = V5_SYSCTRL_BASE + 0x340, /* HPM_STORAGE_REG */
        .value_shift = 0,
        .value_mask = 0x03FF,
        .window_min = 50,
        .window_max = 600,
        .bin_min = 210, /* CORE_HPM_BOUND_10_ESMT */
        .bin_max = 310,
        .aux_reg = V5_HPM_BASE + 0x38, /* HPM_CORE_REG0 (live read) */
        .aux_name = "hpm_core_reg0",
    },
};

/* DRAM type encoding -- PHY+0x2C bits[3:0] (PHY_DRAMCFG_TYPE_MASK).
 * Two encodings coexist in vendor headers:
 *   0..6 follow the [2:0] historical table (DDR1..LPDDR4)
 *   0xA  is DDR4 (introduced with [3:0] mask). */
static const char *v5_dram_type_name(uint32_t t) {
    switch (t) {
    case 0x0:
        return "DDR1";
    case 0x1:
        return "DDR2";
    case 0x2:
        return "DDR3";
    case 0x3:
        return "DDR3L";
    case 0x4:
        return "LPDDR1";
    case 0x5:
        return "LPDDR2/LPDDR3";
    case 0x6:
        return "LPDDR4";
    case 0xa:
        return "DDR4";
    default:
        return "unknown";
    }
}

/* Decode the actual DDR data rate.  The chain is:
 *   ddr_cksel    (CRG+0x2000 bits [18:16])  -- 0 = 24 MHz, 1 = clk_dpll_pst
 *   DPLL VCO     (CRG+0x180 / 0x184, post-divided)  -- already decoded as
 * ddr_pll dficlk_ratio (DDR_PHY0+0x78 bits [1:0]) -- 0=1:1, 1=1:2, 2=1:4 DRAM
 * data rate (MT/s) = phy_clk_mhz * (1 << dficlk_ratio).  In the typical config
 * (DDR3/DDR4 -> 1:2, LPDDR4 -> 1:4) the multiplier matches the DDR burst
 * factor, so this is also "post-divided PLL frequency times the bus
 * double-data-rate". */
static void v5_extra(cJSON *root, bool brief) {
    uint32_t crg_cksel = 0;
    uint32_t phy_ctrl0 = 0;
    uint32_t phy_dramcfg = 0;
    uint32_t dpll0 = 0, dpll1 = 0;
    bool ok = mem_reg(V5_CRG_BASE + 0x2000, &crg_cksel, OP_READ) &&
              mem_reg(V5_CRG_BASE + 0x0180, &dpll0, OP_READ) &&
              mem_reg(V5_CRG_BASE + 0x0184, &dpll1, OP_READ) &&
              mem_reg(V5_DDR_PHY0_BASE + 0x0078, &phy_ctrl0, OP_READ) &&
              mem_reg(V5_DDR_PHY0_BASE + 0x002C, &phy_dramcfg, OP_READ);
    if (!ok)
        return;

    uint32_t ddr_cksel = (crg_cksel >> 16) & 0x7;
    uint32_t dficlk_ratio = phy_ctrl0 & 0x3;
    uint32_t dram_type = phy_dramcfg & 0xF;

    unsigned fbdiv = dpll1 & 0xFFF;
    unsigned refdiv = (dpll1 >> 12) & 0x3F;
    if (!refdiv)
        refdiv = 1;
    unsigned p1 = (dpll0 >> 24) & 0x7;
    if (!p1)
        p1 = 1;
    unsigned p2 = (dpll0 >> 28) & 0x7;
    if (!p2)
        p2 = 1;
    unsigned fracdiv = dpll0 & 0xFFFFFF;
    double dpll_mhz = 24000.0 * (fbdiv + (double)fracdiv / (double)(1u << 24)) /
                      (double)(refdiv * p1 * p2) / 1000.0;

    /* Source: at boot/training time ddr_cksel can momentarily switch to
     * 24 MHz; in normal operation it stays on the DPLL post-divider. */
    double phy_clk_mhz = (ddr_cksel == 0) ? 24.0 : dpll_mhz;
    double data_rate_mtps = phy_clk_mhz * (double)(1u << dficlk_ratio);

    cJSON *ddr = cJSON_CreateObject();
    cJSON *j_inner = ddr;
    ADD_PARAM("dram_type", v5_dram_type_name(dram_type));
    ADD_PARAM_NUM("data_rate_mtps", data_rate_mtps);
    if (!brief) {
        ADD_PARAM_FMT("phy_ctrl0", "0x%08x", phy_ctrl0);
        ADD_PARAM_FMT("dramcfg", "0x%08x", phy_dramcfg);
        ADD_PARAM_FMT("crg_ddr_cksel", "0x%08x", crg_cksel);
        ADD_PARAM_NUM("dficlk_ratio", dficlk_ratio);
        ADD_PARAM("source", ddr_cksel == 0   ? "24MHz"
                            : ddr_cksel == 1 ? "clk_dpll_pst"
                                             : "unknown");
    }
    cJSON_AddItemToObject(root, "ddr", ddr);

    /* SSMOD (DPLL spread-spectrum) status. */
    uint32_t ssmod_raw = 0;
    if (mem_reg(V5_CRG_BASE + V5_SSMOD_CTRL_OFF, &ssmod_raw, OP_READ)) {
        bool cken = ssmod_raw & 0x1;
        bool disable = (ssmod_raw >> 2) & 0x1;
        bool active = cken && !disable;
        cJSON *ss = cJSON_CreateObject();
        {
            cJSON *j_inner = ss;
            cJSON_AddItemToObject(j_inner, "enabled", cJSON_CreateBool(active));
            if (!brief) {
                ADD_PARAM_FMT("reg", "0x%08x", V5_CRG_BASE + V5_SSMOD_CTRL_OFF);
                ADD_PARAM_FMT("raw", "0x%08x", ssmod_raw);
                cJSON_AddItemToObject(j_inner, "ssmod_cken",
                                      cJSON_CreateBool(cken));
                cJSON_AddItemToObject(j_inner, "ssmod_disable",
                                      cJSON_CreateBool(disable));
            }
        }
        cJSON_AddItemToObject(root, "ssmod", ss);
    }

    /* HPM annotations layered onto the standard hpm decoder output:
     *   source       -- where HPM_STORAGE_REG bits[9:0] came from.
     *                   bit[30] (u_hpm_storage_reg.use_board_hpm) tells us
     *                   whether the value reflects an in-die measurement
     *                   averaged by boot software ("chip") or a board
     *                   fixture's calibration override ("board"). The
     *                   "chip" value is the real silicon process bin;
     *                   the "board" value is whatever the manufacturing
     *                   line decided to ship.
     *   svb_version  -- SVB_VER_REG bits[5:2] (u_svb_version_reg.svb_type),
     *                   per svb.h's enum product_type. The binning
     *                   threshold for "this die is fast enough" depends
     *                   on the SVB version (CORE_HPM_BOUND_10 / _20 /
     *                   _10_ESMT in svb.h). When this is "none" the
     *                   firmware isn't running SVB at all and the bin
     *                   classification is informational only. */
    cJSON *hpm = cJSON_GetObjectItemCaseSensitive(root, "hpm");
    if (hpm) {
        uint32_t hpm_storage = 0;
        uint32_t svb_ver = 0;
        bool use_board_hpm = false;
        if (mem_reg(V5_SYSCTRL_BASE + 0x340, &hpm_storage, OP_READ))
            use_board_hpm = (hpm_storage >> 30) & 0x1;
        const char *svb_name = "none";
        if (mem_reg(V5_SYSCTRL_BASE + 0x168, &svb_ver, OP_READ)) {
            switch ((svb_ver >> 2) & 0xF) {
            case 1:
                svb_name = "10";
                break;
            case 2:
                svb_name = "20";
                break;
            case 3:
                svb_name = "00";
                break;
            case 4:
                svb_name = "608";
                break;
            default:
                svb_name = "none";
                break;
            }
        }
        cJSON *j_inner = hpm;
        ADD_PARAM("source", use_board_hpm ? "board" : "chip");
        ADD_PARAM("svb_version", svb_name);
    }

    /* Per-site HPM live readings -- only in full output. The canonical
     * core_hpm_value at HPM_STORAGE_REG is already surfaced by the
     * generic hpm decoder via v5_hpms[]. */
    if (!brief) {
        struct site {
            const char *name;
            uint32_t reg0;
            uint32_t reg1;
        } sites[] = {
            {"npu", V5_HPM_BASE + 0x18, V5_HPM_BASE + 0x1C},
            {"mda", V5_HPM_BASE + 0x28, V5_HPM_BASE + 0x2C},
            {"core", V5_HPM_BASE + 0x38, V5_HPM_BASE + 0x3C},
        };
        cJSON *sites_j = cJSON_CreateObject();
        for (size_t i = 0; i < sizeof(sites) / sizeof(sites[0]); i++) {
            uint32_t r0 = 0, r1 = 0;
            if (!mem_reg(sites[i].reg0, &r0, OP_READ) ||
                !mem_reg(sites[i].reg1, &r1, OP_READ))
                continue;
            cJSON *site = cJSON_CreateObject();
            {
                cJSON *j_inner = site;
                /* u_hpm_reg: bits[9:0]=hpm0, bits[25:16]=hpm1 */
                ADD_PARAM_NUM("reg0_hpm0", r0 & 0x3FF);
                ADD_PARAM_NUM("reg0_hpm1", (r0 >> 16) & 0x3FF);
                ADD_PARAM_NUM("reg1_hpm0", r1 & 0x3FF);
                ADD_PARAM_NUM("reg1_hpm1", (r1 >> 16) & 0x3FF);
            }
            cJSON_AddItemToObject(sites_j, sites[i].name, site);
        }
        cJSON_AddItemToObject(root, "hpm_sites", sites_j);
    }
}

const struct clock_family clocks_family_v5 = {
    .chip_id = HISI_OT,
    .label = "Hisilicon V5 (3516CV610 / 3519DV500)",
    .plls = v5_plls,
    .n_plls = sizeof(v5_plls) / sizeof(v5_plls[0]),
    .muxes = NULL,
    .n_muxes = 0,
    .hpms = v5_hpms,
    .n_hpms = sizeof(v5_hpms) / sizeof(v5_hpms[0]),
    .extra = v5_extra,
};
