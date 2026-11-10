/* NA51089's direct temperature read, driven against a model of the eFuse
 * controller and the SAR ADC.
 *
 * The model answers the way the lab NT98566 did: the ADC gated and idle from
 * boot, its thermal average reading 0 until channel 3 is converting, and eFuse
 * row 6 holding 0x000080E4. Writes are checked as they happen, so a write that
 * could program a fuse, or one that takes away a clock or a converter someone
 * else started, names itself where it is made.
 *
 * CHECK rather than assert(), because the release flags carry -DNDEBUG and
 * would compile an assert-based test into one that passes unconditionally.
 *
 * novatek.c is included rather than linked, for the same reason
 * hal_hisi_test.c includes hal_hisi.c: the readers are file-statics, and a
 * seam on mem_reg() would cost an indirect call in every ipcinfo. */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hal/novatek.c"

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                        \
        }                                                                      \
    } while (0)

/* ---- the model --------------------------------------------------------- */

static struct {
    uint32_t cg_en1, adc_ctrl;
    uint32_t efuse_mode, efuse_row, efuse_data, efuse_ctrl, efuse_sts;
    uint32_t fuse[32];
    uint32_t code; /* what channel 3 averages to once it converts */
} M;

static int efuse_writes, adc_writes, cg_writes;
static const char *illegal;

static void fresh(void) {
    memset(&M, 0, sizeof M);
    /* the lab camera, nightly 2026-10-21, as found */
    M.cg_en1 = 0xB0440FF1;
    M.adc_ctrl = 0x5000F000;
    M.efuse_sts = 0x0000000C;
    M.fuse[6] = 0x000080E4;
    M.code = 265;
    efuse_writes = adc_writes = cg_writes = 0;
    illegal = NULL;
    chip_generation = CHIP_NA51089;
}

static bool converting(void) {
    const uint32_t on = NA51089_ADC_AIN3_MODE | NA51089_ADC_EN;
    return (M.cg_en1 & NA51089_CG_ADC) && (M.adc_ctrl & on) == on;
}

bool mem_reg(uint32_t addr, uint32_t *data, enum REG_OPS op) {
    if (op == OP_READ) {
        switch (addr) {
        case NA51089_CG_CLK_EN1:
            *data = M.cg_en1;
            return true;
        case NA51089_ADC_CTRL:
            *data = M.adc_ctrl;
            return true;
        case NA51089_ADC_THERMAL:
            *data = 0x30 | (converting() ? M.code << 16 : 0);
            return true;
        case EFUSE_MODE:
            *data = M.efuse_mode;
            return true;
        case EFUSE_ROW:
            *data = M.efuse_row;
            return true;
        case EFUSE_DATA:
            *data = M.efuse_data;
            return true;
        case EFUSE_CTRL:
            *data = M.efuse_ctrl;
            return true;
        case EFUSE_STS:
            *data = M.efuse_sts;
            return true;
        }
        return false;
    }

    const uint32_t v = *data;
    switch (addr) {
    case NA51089_CG_CLK_EN1:
        cg_writes++;
        if (M.cg_en1 & ~v)
            illegal = "gated a clock that was running";
        M.cg_en1 = v;
        return true;
    case NA51089_ADC_CTRL:
        adc_writes++;
        if (M.adc_ctrl & ~v)
            illegal = "cleared an ADC control bit that was set";
        M.adc_ctrl = v;
        return true;
    }

    efuse_writes++;
    switch (addr) {
    case EFUSE_MODE:
        /* efuse_open()'s first argument, bit 0, selects programming */
        if (v)
            illegal = "eFuse taken out of read mode";
        M.efuse_mode = v;
        return true;
    case EFUSE_ROW:
        M.efuse_row = v;
        return true;
    case EFUSE_CTRL:
        if ((v & 0x10) && !(M.efuse_ctrl & 1))
            illegal = "eFuse read triggered while closed";
        if ((v & 0x10) && M.efuse_mode)
            illegal = "eFuse triggered outside read mode";
        M.efuse_ctrl = v & ~0x10u; /* the trigger is self-clearing */
        M.efuse_sts = (M.efuse_sts & ~1u) | (v & 1);
        if (v & 0x10) {
            M.efuse_data = M.fuse[M.efuse_row & 0x1f];
            M.efuse_sts |= 2;
        }
        return true;
    case EFUSE_STS:
        M.efuse_sts &= ~(v & 2); /* write-one-to-clear */
        return true;
    }
    return false;
}

/* The rest of novatek.c's externals. disable_printk() is not optional: the
 * real one silences the host's kernel log if this ever runs as root. */
bool line_from_file(const char *filename, const char *re, char *buf, size_t n) {
    (void)filename;
    (void)re;
    (void)buf;
    (void)n;
    return false;
}
void disable_printk(void) {}
void restore_printk(void) {}
int chip_generation;
char chip_name[128];

static bool efuse_closed(void) {
    return !(M.efuse_ctrl & 1) && !(M.efuse_sts & 1) && M.efuse_row == 0;
}

/* (code - trim) * 1.319 + 30 */
static bool near(float t, int code, int trim) {
    return fabsf(t - ((code - trim) * 1.319f + 30)) < 0.01f;
}

int main(void) {
    /* novatek_setup_hal() is what installs the reader, so go through it. */
    fresh();
    novatek_setup_hal();
    CHECK(hal_temperature == na51089_get_temp);

    /* The lab camera: ADC gated and idle, trim 0xE4 fused. Started as the
     * RTOS side starts it, read, eFuse left closed: 78.80 degC. */
    float t = hal_temperature();
    CHECK(near(t, 265, 228));
    CHECK(M.cg_en1 == (0xB0440FF1 | NA51089_CG_ADC));
    CHECK(M.adc_ctrl == 0x5080F008);
    CHECK(efuse_closed());
    CHECK(illegal == NULL);

    /* A second read touches neither the clock nor the ADC. */
    cg_writes = adc_writes = 0;
    t = hal_temperature();
    CHECK(near(t, 265, 228));
    CHECK(cg_writes == 0 && adc_writes == 0);

    /* An ADC someone else runs is read, not reconfigured: the vendor driver
     * leaves clkdiv 1 and all four channels continuous. */
    fresh();
    M.cg_en1 |= NA51089_CG_ADC;
    M.adc_ctrl = 0x5080100F;
    t = na51089_get_temp();
    CHECK(near(t, 265, 228));
    CHECK(cg_writes == 0 && adc_writes == 0);
    CHECK(illegal == NULL);

    /* Converting but with channel 3 one-shot: only ain3_mode is added. */
    fresh();
    M.cg_en1 |= NA51089_CG_ADC;
    M.adc_ctrl = 0x50801007;
    t = na51089_get_temp();
    CHECK(near(t, 265, 228));
    CHECK(M.adc_ctrl == 0x5080100F);
    CHECK(illegal == NULL);

    /* An unfused die gets nvt_adc's default. */
    fresh();
    M.fuse[6] = 0;
    CHECK(near(na51089_get_temp(), 265, NA51089_DEFAULT_TRIM));

    /* The first copy invalidated (bit 14): the second one is used. */
    fresh();
    M.fuse[6] = (0x80E0u << 16) | 0xC0E4;
    CHECK(near(na51089_get_temp(), 265, 0xE0));

    /* Neither copy good: the default, not a stray 14-bit value. */
    fresh();
    M.fuse[6] = (0xC0E0u << 16) | 0x40E4;
    CHECK(near(na51089_get_temp(), 265, NA51089_DEFAULT_TRIM));

    /* The kernel mid-sequence: the controller is left alone entirely, and
     * the reading falls back to the default trim rather than failing. */
    fresh();
    M.efuse_ctrl = 1;
    M.efuse_sts |= 1;
    t = na51089_get_temp();
    CHECK(near(t, 265, NA51089_DEFAULT_TRIM));
    CHECK(efuse_writes == 0);

    /* A converter that never produces a sample is no reading. */
    fresh();
    M.code = 0;
    CHECK(isnan(na51089_get_temp()));

    /* Every other die keeps the sysfs reader, if the host even has a
     * thermal zone; the registers above are NA51089's alone. */
    fresh();
    chip_generation = CHIP_NA51055;
    hal_temperature = NULL;
    novatek_setup_hal();
    CHECK(hal_temperature != na51089_get_temp);

    if (failures)
        fprintf(stderr, "novatek_test: %d failure(s)\n", failures);
    else
        printf("novatek_test: ok\n");
    return failures ? 1 : 0;
}
