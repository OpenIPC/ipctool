#ifndef CLOCKS_H
#define CLOCKS_H

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

/* Raw register dump entry. Two flavours:
 *   - single register: reg2 = 0
 *   - two-register PLL config pair (e.g. {APLL,DPLL,EPLL,VPLL}_CONFIG_0/1
 *     in the HiSilicon CRG): set both reg (CONFIG_0) and reg2 (CONFIG_1)
 *
 * The two-register form is for PLLs whose FBDIV/REFDIV bit layout we
 * haven't verified yet — we dump the raw words so users can correlate
 * with vendor docs without us shipping a wrong decode. */
struct raw_reg_info {
    const char *name;
    const char *label;
    uint32_t reg;
    uint32_t reg2; /* 0 = single-register entry */
    const char *note;
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
    const struct raw_reg_info *raws;
    size_t n_raws;
};

/* Builds the cJSON tree for the current chip. Returns NULL on unsupported
 * chip family. Used by both the `clocks`/`freq` subcommand and the default
 * `ipctool` YAML survey. */
cJSON *clocks_build_json(void);

int clocks_cmd(int argc, char **argv);

#endif /* CLOCKS_H */
