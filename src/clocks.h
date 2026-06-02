#ifndef CLOCKS_H
#define CLOCKS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cjson/cJSON.h>

/* PLL descriptor matching the HiSilicon `*_pll_clock` layout (e.g.
 * `struct hi3516a_pll_clock` in the Hi3516A SDK kernel patch). One PLL spans
 * two control registers:
 *   ctrl_reg1: FRACDIV, POSTDIV1, POSTDIV2
 *   ctrl_reg2: FBDIV, REFDIV
 * Frequency:  f = input_khz * FBDIV / (REFDIV * POSTDIV1 * POSTDIV2)
 *
 * For families that use a single-register PLL, set ctrl_reg2 == 0 and put
 * the relevant fields on ctrl_reg1 (frac_width may be 0 to skip FRACDIV).
 * Set any *_width = 0 to default that field to 1 (treat as absent). */
struct pll_info {
    const char *name;  /* JSON key, e.g. "cpu_pll" */
    const char *label; /* human label, e.g. "CPU PLL (APLL)" */
    uint32_t ctrl_reg1;
    uint8_t frac_shift;
    uint8_t frac_width; /* 0 = no FRACDIV */
    uint8_t postdiv1_shift;
    uint8_t postdiv1_width; /* 0 = postdiv1 fixed to 1 */
    uint8_t postdiv2_shift;
    uint8_t postdiv2_width; /* 0 = postdiv2 fixed to 1 */
    uint32_t ctrl_reg2;     /* 0 = FBDIV/REFDIV live on ctrl_reg1 */
    uint8_t fbdiv_shift;
    uint8_t fbdiv_width;
    uint8_t refdiv_shift;
    uint8_t refdiv_width; /* 0 = refdiv fixed to 1 */
    uint32_t input_khz;   /* crystal frequency; 24000 on V4 */
    uint32_t lock_reg;    /* 0 = no lock-bit check (e.g. PERI_CRG_PLL122) */
    uint8_t lock_bit;     /* bit index in lock_reg */
};

struct mux_entry {
    uint8_t sel;
    uint16_t mhz;
};

struct mux_info {
    const char *name;
    const char *label;
    uint32_t reg;
    uint8_t sel_shift;
    uint8_t sel_mask;
    const struct mux_entry *table;
    size_t table_len;
    uint8_t rate_mult; /* DDR3 = 4 (quad-pumped); 0 = no data_rate */
};

struct hpm_info {
    const char *name;
    const char *label;
    uint32_t reg;
    uint8_t value_shift;
    uint16_t value_mask;
    uint16_t window_min; /* HPM_CORE_MIN — outer validity */
    uint16_t window_max; /* HPM_CORE_MAX */
    uint16_t bin_min;    /* HPM_CORE_VALUE_MIN — nominal binning low */
    uint16_t bin_max;    /* HPM_CORE_VALUE_MAX — nominal binning high */
    uint32_t aux_reg;    /* 0 = none */
    const char *aux_name;
};

struct clock_family {
    int chip_id; /* matches chip_generation, e.g. HISI_V4 */
    const char *label;
    const struct pll_info *plls;
    size_t n_plls;
    const struct mux_info *muxes;
    size_t n_muxes;
    const struct hpm_info *hpms;
    size_t n_hpms;
    /* Optional family-specific extras (e.g. composite DDR rate that
     * depends on a CRG mux + a PHY register). Called after the standard
     * pll/mux/hpm sections; cJSON children appended into the same root. */
    void (*extra)(cJSON *root, bool brief);
};

/* Builds the cJSON tree for the current chip. Returns NULL on unsupported
 * chip family.
 *
 *   brief = true  : only the headline numbers (cpu_pll.freq_mhz,
 *                   ddr.data_rate_mbps, hpm.bin) -- used by the default
 *                   `ipctool` no-arg survey to keep its YAML compact.
 *   brief = false : full detail (raw register values, all PLL fields,
 *                   HPM aux register, etc.) -- used by `ipctool clocks`. */
cJSON *clocks_build_json(bool brief);

int clocks_cmd(int argc, char **argv);

#endif /* CLOCKS_H */
