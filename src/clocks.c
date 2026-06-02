/* `ipctool clocks` (alias `freq`) — show CPU/peripheral PLL frequencies, DDR
 * clock, and per-die HPM characterization. Implements OpenIPC/ipctool#161. */

#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chipid.h"
#include "cjson/cJSON.h"
#include "cjson/cYAML.h"
#include "clocks.h"
#include "tools.h"

extern const struct clock_family clocks_family_v4;
extern const struct clock_family clocks_family_v4a;
extern const struct clock_family clocks_family_v5;

/* TODO: add V1/V2/V3/V3A/3536C/3536D tables — they share the
 * CRG-table approach but use different bases and bit layouts. */
static const struct clock_family *const families[] = {
    &clocks_family_v4,
    &clocks_family_v4a,
    &clocks_family_v5,
};

static const struct clock_family *family_for_chip(int chip_id) {
    for (size_t i = 0; i < ARRCNT(families); i++) {
        if (families[i]->chip_id == chip_id)
            return families[i];
    }
    return NULL;
}

static void add_read_failure(cJSON *parent, const char *key) {
    cJSON_AddItemToObject(parent, key, cJSON_CreateString("<read failed>"));
}

static uint32_t extract_field(uint32_t raw, uint8_t shift, uint8_t width) {
    if (width == 0)
        return 0;
    uint32_t mask = (width >= 32) ? 0xFFFFFFFFu : ((1u << width) - 1u);
    return (raw >> shift) & mask;
}

static cJSON *decode_pll(const struct pll_info *pll, bool brief) {
    cJSON *j_inner = cJSON_CreateObject();

    /* Read ctrl_reg1 (FRACDIV / POSTDIV1 / POSTDIV2) */
    uint32_t raw1 = 0;
    bool ok1 = mem_reg(pll->ctrl_reg1, &raw1, OP_READ);
    /* Read ctrl_reg2 if present (FBDIV / REFDIV); else FBDIV/REFDIV live on
     * ctrl_reg1 (single-register PLLs used by older HiSi variants). */
    uint32_t raw2 = raw1;
    bool ok2 = true;
    if (pll->ctrl_reg2 && pll->ctrl_reg2 != pll->ctrl_reg1)
        ok2 = mem_reg(pll->ctrl_reg2, &raw2, OP_READ);

    if (!ok1 || !ok2) {
        ADD_PARAM("error", "register read failed");
        return j_inner;
    }

    uint32_t fracdiv = extract_field(raw1, pll->frac_shift, pll->frac_width);
    uint32_t pdiv1 =
        extract_field(raw1, pll->postdiv1_shift, pll->postdiv1_width);
    uint32_t pdiv2 =
        extract_field(raw1, pll->postdiv2_shift, pll->postdiv2_width);
    uint32_t fbdiv = extract_field(raw2, pll->fbdiv_shift, pll->fbdiv_width);
    uint32_t refdiv = extract_field(raw2, pll->refdiv_shift, pll->refdiv_width);
    if (pll->postdiv1_width == 0)
        pdiv1 = 1;
    if (pll->postdiv2_width == 0)
        pdiv2 = 1;
    if (pll->refdiv_width == 0)
        refdiv = 1;

    /* Compute frequency. f = input * (FBDIV + FRACDIV/2^frac_width) /
     * (REFDIV * POSTDIV1 * POSTDIV2). */
    double freq_mhz = 0.0;
    uint32_t denom = refdiv * pdiv1 * pdiv2;
    if (fbdiv != 0 && denom != 0) {
        uint64_t numer_khz = (uint64_t)pll->input_khz * fbdiv;
        if (pll->frac_width)
            numer_khz +=
                ((uint64_t)pll->input_khz * fracdiv) >> pll->frac_width;
        freq_mhz = (double)numer_khz / (double)denom / 1000.0;
    }

    if (brief) {
        ADD_PARAM_NUM("freq_mhz", freq_mhz);
        return j_inner;
    }

    ADD_PARAM_FMT("ctrl_reg1", "0x%08x", pll->ctrl_reg1);
    ADD_PARAM_FMT("ctrl_reg1_raw", "0x%08x", raw1);
    if (pll->ctrl_reg2 && pll->ctrl_reg2 != pll->ctrl_reg1) {
        ADD_PARAM_FMT("ctrl_reg2", "0x%08x", pll->ctrl_reg2);
        ADD_PARAM_FMT("ctrl_reg2_raw", "0x%08x", raw2);
    }
    ADD_PARAM_NUM("fbdiv", fbdiv);
    ADD_PARAM_NUM("refdiv", refdiv);
    ADD_PARAM_NUM("postdiv1", pdiv1);
    ADD_PARAM_NUM("postdiv2", pdiv2);
    if (pll->frac_width)
        ADD_PARAM_FMT("fracdiv", "0x%06x", fracdiv);
    ADD_PARAM_NUM("freq_mhz", freq_mhz);

    /* Optional lock-bit check (e.g. PERI_CRG_PLL122 bit 0 = APLL on V4). */
    if (pll->lock_reg) {
        uint32_t lock_raw;
        if (mem_reg(pll->lock_reg, &lock_raw, OP_READ)) {
            bool locked = (lock_raw >> pll->lock_bit) & 1u;
            cJSON_AddItemToObject(j_inner, "locked", cJSON_CreateBool(locked));
        }
    }
    return j_inner;
}

static cJSON *decode_mux(const struct mux_info *mux, bool brief) {
    cJSON *j_inner = cJSON_CreateObject();
    uint32_t raw;
    if (!mem_reg(mux->reg, &raw, OP_READ)) {
        ADD_PARAM("error", "register read failed");
        return j_inner;
    }
    uint8_t sel = (raw >> mux->sel_shift) & mux->sel_mask;

    uint16_t mhz = 0;
    bool found = false;
    for (size_t i = 0; i < mux->table_len; i++) {
        if (mux->table[i].sel == sel) {
            mhz = mux->table[i].mhz;
            found = true;
            break;
        }
    }

    if (brief) {
        if (mux->rate_mult)
            ADD_PARAM_NUM("data_rate_mbps",
                          (uint32_t)mhz * (found ? mux->rate_mult : 0));
        else
            ADD_PARAM_NUM("freq_mhz", mhz);
        return j_inner;
    }

    ADD_PARAM_FMT("reg", "0x%08x", mux->reg);
    ADD_PARAM_FMT("raw", "0x%08x", raw);
    ADD_PARAM_NUM("cksel", sel);
    if (found) {
        ADD_PARAM_NUM("freq_mhz", mhz);
        if (mux->rate_mult)
            ADD_PARAM_NUM("data_rate_mbps", (uint32_t)mhz * mux->rate_mult);
    }
    return j_inner;
}

static const char *hpm_bin(uint16_t v, const struct hpm_info *h) {
    if (v < h->window_min || v > h->window_max)
        return "out_of_spec";
    if (v < h->bin_min)
        return "below_window";
    if (v > h->bin_max)
        return "above_window";
    /* Split [bin_min..bin_max] into thirds. */
    uint32_t span = h->bin_max - h->bin_min;
    uint32_t t = (uint32_t)(v - h->bin_min) * 3;
    if (t < span)
        return "low";
    if (t < span * 2)
        return "mid";
    return "high";
}

static cJSON *decode_hpm(const struct hpm_info *h, bool brief) {
    cJSON *j_inner = cJSON_CreateObject();
    uint32_t raw;
    if (!mem_reg(h->reg, &raw, OP_READ) || raw == 0xFFFFFFFF) {
        /* HPM register absent on this variant — caller treats NULL by
         * omitting the section entirely. */
        cJSON_Delete(j_inner);
        return NULL;
    }
    uint16_t value = (raw >> h->value_shift) & h->value_mask;
    const char *bin = hpm_bin(value, h);

    if (brief) {
        ADD_PARAM("bin", bin);
        return j_inner;
    }

    ADD_PARAM_FMT("reg", "0x%08x", h->reg);
    ADD_PARAM_FMT("raw", "0x%08x", raw);
    ADD_PARAM_NUM("value", value);
    ADD_PARAM("bin", bin);

    cJSON *window = cJSON_CreateArray();
    cJSON_AddItemToArray(window, cJSON_CreateNumber(h->bin_min));
    cJSON_AddItemToArray(window, cJSON_CreateNumber(h->bin_max));
    cJSON_AddItemToObject(j_inner, "binning_window", window);

    if (h->aux_reg) {
        uint32_t aux;
        if (mem_reg(h->aux_reg, &aux, OP_READ)) {
            ADD_PARAM_FMT("aux_reg", "0x%08x", h->aux_reg);
            ADD_PARAM_FMT("aux_value", "0x%08x", aux);
            if (h->aux_name)
                ADD_PARAM("aux_name", h->aux_name);
        } else if (h->aux_name) {
            add_read_failure(j_inner, h->aux_name);
        }
    }
    return j_inner;
}

static bool read_uint_from_file(const char *path, uint32_t *out) {
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    unsigned long v;
    bool ok = fscanf(f, "%lu", &v) == 1;
    fclose(f);
    if (ok)
        *out = (uint32_t)v;
    return ok;
}

static bool read_first_token_from_file(const char *path, char *buf,
                                       size_t buflen) {
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    bool ok = false;
    if (fgets(buf, (int)buflen, f)) {
        size_t n = strlen(buf);
        while (n && (buf[n - 1] == '\n' || buf[n - 1] == ' '))
            buf[--n] = '\0';
        ok = n > 0;
    }
    fclose(f);
    return ok;
}

static cJSON *build_cpu_running(void) {
    /* TODO: also report per-core scaling_cur_freq on SMP variants. */
    uint32_t khz = 0;
    if (!read_uint_from_file(
            "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", &khz))
        return NULL;

    cJSON *j_inner = cJSON_CreateObject();
    ADD_PARAM_NUM("freq_mhz", khz / 1000.0);

    char gov[64];
    if (read_first_token_from_file(
            "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", gov,
            sizeof(gov))) {
        ADD_PARAM("governor", gov);
    }
    return j_inner;
}

cJSON *clocks_build_json(bool brief) {
    /* Make sure chip detection has run and chip_generation is populated. */
    if (!getchipname())
        return NULL;

    const struct clock_family *fam = family_for_chip(chip_generation);
    if (!fam)
        return NULL;

    cJSON *j_inner = cJSON_CreateObject();

    for (size_t i = 0; i < fam->n_plls; i++) {
        /* Brief survey only shows the headline (CPU) PLL -- by convention
         * the first entry in the family's pll table. Other PLLs (DDR /
         * ETH / VIDEO) are detail and only emitted by the full `ipctool
         * clocks` output. */
        if (brief && i > 0)
            continue;
        cJSON *p = decode_pll(&fam->plls[i], brief);
        cJSON_AddItemToObject(j_inner, fam->plls[i].name, p);
    }
    for (size_t i = 0; i < fam->n_muxes; i++) {
        cJSON *m = decode_mux(&fam->muxes[i], brief);
        cJSON_AddItemToObject(j_inner, fam->muxes[i].name, m);
    }
    for (size_t i = 0; i < fam->n_hpms; i++) {
        cJSON *h = decode_hpm(&fam->hpms[i], brief);
        if (h)
            cJSON_AddItemToObject(j_inner, fam->hpms[i].name, h);
    }

    if (fam->extra)
        fam->extra(j_inner, brief);

    if (!brief) {
        cJSON *running = build_cpu_running();
        if (running)
            cJSON_AddItemToObject(j_inner, "cpu_running", running);
    }
    return j_inner;
}

static void print_clocks_usage(const char *prog) {
    printf("Usage: %s clocks [--json]\n"
           "       %s freq   [--json]\n"
           "\n"
           "Show CPU PLL, peripheral PLL, DDR clock and per-die HPM\n"
           "characterization (mask-ROM PLL binning) of the running SoC.\n"
           "\n"
           "Output is YAML by default; --json emits JSON.\n"
           "Currently supported: Hisilicon V4 / Goke V300 family.\n",
           prog, prog);
}

int clocks_cmd(int argc, char **argv) {
    bool want_json = false;

    const struct option long_options[] = {
        {"json", no_argument, NULL, 'j'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };
    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "jh", long_options, NULL)) != -1) {
        switch (opt) {
        case 'j':
            want_json = true;
            break;
        case 'h':
            print_clocks_usage("ipctool");
            return EXIT_SUCCESS;
        default:
            print_clocks_usage("ipctool");
            return EXIT_FAILURE;
        }
    }

    /* getchipname runs once for the lifetime of the process; calling it here
     * makes sure chip_generation is set before we dispatch. */
    if (!getchipname()) {
        fprintf(stderr, "clocks: cannot identify SoC\n");
        return EXIT_FAILURE;
    }

    if (!family_for_chip(chip_generation)) {
        fprintf(stderr,
                "clocks: chip family 0x%x (%s) not supported yet; only "
                "Hisilicon V4 / Goke V300 is implemented\n",
                chip_generation, chip_name);
        return EXIT_FAILURE;
    }

    cJSON *clocks = clocks_build_json(false);
    if (!clocks) {
        fprintf(stderr, "clocks: failed to build clock info\n");
        return EXIT_FAILURE;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "clocks", clocks);

    char *out = want_json ? cJSON_Print(root) : cYAML_Print(root);
    if (out) {
        printf("%s", out);
        if (want_json)
            printf("\n");
        free(out);
    }
    cJSON_Delete(root);
    return EXIT_SUCCESS;
}
