/* The NA51055/NA51084/NA51089 direct temperature read, driven against a model
 * of the eFuse controller and the SAR ADC.
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
    int pending;   /* STS reads until a triggered read completes */
    int reads_of_row6;
    uint32_t second_read; /* if set, what an interleaved second read sees */
} M;

static int efuse_writes, adc_writes, cg_writes, mmio_calls;
static const char *illegal;
static bool mmio_fails;      /* /dev/mem withheld */
static const char *dt_model; /* line_from_file's answers */
static struct {
    const char *type, *temp; /* NULL type: no such zone */
} zone[4];

static void fresh(void) {
    memset(&M, 0, sizeof M);
    /* the lab camera, nightly 2026-10-21, as found */
    M.cg_en1 = 0xB0440FF1;
    M.adc_ctrl = 0x5000F000;
    M.efuse_sts = 0x0000000C;
    M.fuse[6] = 0x000080E4;
    M.code = 265;
    efuse_writes = adc_writes = cg_writes = mmio_calls = 0;
    illegal = NULL;
    mmio_fails = false;
    dt_model = NULL;
    memset(zone, 0, sizeof zone);
    chip_generation = CHIP_NA51089;
    nvt_trim = -1;
}

static bool converting(void) {
    const uint32_t on = NVT_ADC_AIN3_MODE | NVT_ADC_EN;
    return (M.cg_en1 & NVT_CG_ADC) && (M.adc_ctrl & on) == on;
}

static void complete_read(void) {
    const uint32_t row = M.efuse_row & 0x1f;
    M.efuse_data = M.fuse[row];
    if (row == 6 && M.reads_of_row6++ && M.second_read)
        M.efuse_data = M.second_read;
    M.efuse_sts |= 2;
}

bool mem_reg(uint32_t addr, uint32_t *data, enum REG_OPS op) {
    mmio_calls++;
    if (mmio_fails)
        return false;
    if (op == OP_READ) {
        /* The read takes a few polls to land, as a real one does, so a
         * read-done bit already standing is not this read's. */
        if (addr == EFUSE_STS && M.pending && !--M.pending)
            complete_read();
        switch (addr) {
        case NVT_CG_CLK_EN1:
            *data = M.cg_en1;
            return true;
        case NVT_ADC_CTRL:
            *data = M.adc_ctrl;
            return true;
        case NVT_ADC_THERMAL:
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
    case NVT_CG_CLK_EN1:
        cg_writes++;
        if (M.cg_en1 & ~v)
            illegal = "gated a clock that was running";
        M.cg_en1 = v;
        return true;
    case NVT_ADC_CTRL:
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
        if (v & 0x10)
            M.pending = 3;
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
    (void)re;
    const char *ans = NULL;
    int i;
    char leaf[8];
    if (!strcmp(filename, "/proc/device-tree/model"))
        ans = dt_model;
    else if (sscanf(filename, "/sys/class/thermal/thermal_zone%d/%7s", &i,
                    leaf) == 2 &&
             i >= 0 && i < 4 && zone[i].type)
        ans = !strcmp(leaf, "type") ? zone[i].type : zone[i].temp;
    if (!ans)
        return false;
    snprintf(buf, n, "%s", ans);
    return true;
}
void disable_printk(void) {}
void restore_printk(void) {}
int chip_generation;
char chip_name[128];

static bool efuse_closed(void) {
    return !(M.efuse_ctrl & 1) && !(M.efuse_sts & 1) && M.efuse_row == 0;
}

/* (code - trim) * 1.319 + 30, NA51084 and NA51089 */
static bool near(float t, int code, int trim) {
    return fabsf(t - ((code - trim) * 1.319f + 30)) < 0.01f;
}

/* (code - trim) * 1.3 + 30, NA51055 */
static bool near55(float t, int code, int trim) {
    return fabsf(t - ((code - trim) * 1.3f + 30)) < 0.01f;
}

int main(void) {
    /* novatek_setup_hal() is what installs the reader, so go through it. */
    fresh();
    novatek_setup_hal();
    CHECK(hal_temperature == nvt_get_temp);

    /* The lab camera: ADC gated and idle, trim 0xE4 fused. Started as the
     * RTOS side starts it, read, eFuse left closed: 78.80 degC. */
    float t = hal_temperature();
    CHECK(near(t, 265, 228));
    CHECK(M.cg_en1 == (0xB0440FF1 | NVT_CG_ADC));
    CHECK(M.adc_ctrl == 0x5080F008);
    CHECK(efuse_closed());
    CHECK(illegal == NULL);

    /* A second read touches neither the clock nor the ADC, and the trim is
     * not read again: every sequence is a window on the kernel's readers. */
    cg_writes = adc_writes = efuse_writes = 0;
    t = hal_temperature();
    CHECK(near(t, 265, 228));
    CHECK(cg_writes == 0 && adc_writes == 0 && efuse_writes == 0);

    /* An ADC someone else runs is read, not reconfigured: the vendor driver
     * leaves clkdiv 1 and all four channels continuous. */
    fresh();
    M.cg_en1 |= NVT_CG_ADC;
    M.adc_ctrl = 0x5080100F;
    t = nvt_get_temp();
    CHECK(near(t, 265, 228));
    CHECK(cg_writes == 0 && adc_writes == 0);
    CHECK(illegal == NULL);

    /* Converting but with channel 3 one-shot: only ain3_mode is added. */
    fresh();
    M.cg_en1 |= NVT_CG_ADC;
    M.adc_ctrl = 0x50801007;
    t = nvt_get_temp();
    CHECK(near(t, 265, 228));
    CHECK(M.adc_ctrl == 0x5080100F);
    CHECK(illegal == NULL);

    /* An unfused die gets nvt_adc's default. */
    fresh();
    M.fuse[6] = 0;
    CHECK(near(nvt_get_temp(), 265, NVT_DEFAULT_TRIM));

    /* The first copy invalidated (bit 14): the second one is used. */
    fresh();
    M.fuse[6] = (0x80E0u << 16) | 0xC0E4;
    CHECK(near(nvt_get_temp(), 265, 0xE0));

    /* Neither copy good: the default, not a stray 14-bit value. */
    fresh();
    M.fuse[6] = (0xC0E0u << 16) | 0x40E4;
    CHECK(near(nvt_get_temp(), 265, NVT_DEFAULT_TRIM));

    /* The kernel mid-sequence: the controller is left alone entirely, and
     * the reading falls back to the default trim rather than failing -- for
     * this call only. Once the kernel is done, the fused trim is read. */
    fresh();
    M.efuse_ctrl = 1;
    M.efuse_sts |= 1;
    t = nvt_get_temp();
    CHECK(near(t, 265, NVT_DEFAULT_TRIM));
    CHECK(efuse_writes == 0);
    M.efuse_ctrl = 0;
    M.efuse_sts &= ~1u;
    CHECK(near(nvt_get_temp(), 265, 228));

    /* A read-done bit someone left standing, with their row still in DATA:
     * that word is not row 6, and must not be taken for it. */
    fresh();
    M.efuse_sts |= 2;
    M.efuse_data = 0x00008007; /* row 7, IDDQ: a "valid" trim of 7 */
    CHECK(near(nvt_get_temp(), 265, 228));
    CHECK(illegal == NULL);

    /* Two reads that disagree are someone else's sequence interleaved with
     * ours: no calibration rather than the wrong one. */
    fresh();
    M.second_read = 0x000080F0;
    CHECK(near(nvt_get_temp(), 265, NVT_DEFAULT_TRIM));
    CHECK(M.reads_of_row6 == 2);
    M.second_read = 0;
    CHECK(near(nvt_get_temp(), 265, 228));

    /* An unfused die is an answer, not a failure: kept like a fused one. */
    fresh();
    M.fuse[6] = 0;
    CHECK(near(nvt_get_temp(), 265, NVT_DEFAULT_TRIM));
    efuse_writes = 0;
    CHECK(near(nvt_get_temp(), 265, NVT_DEFAULT_TRIM));
    CHECK(efuse_writes == 0);

    /* The kernel carries nvt_adc (OpenIPC/firmware#2950, with the units of
     * OpenIPC/linux#323): its zone is read, and no register is touched. */
    fresh();
    zone[0].type = "nvt_thermal";
    zone[0].temp = "47147";
    CHECK(fabsf(nvt_get_temp() - 47.147f) < 0.001f);
    CHECK(mmio_calls == 0);

    /* The vendor's own kernel, in whole degrees. */
    zone[0].temp = "52";
    CHECK(nvt_get_temp() == 52.0f);
    CHECK(mmio_calls == 0);

    /* Below zero either way: nvt_adc's millidegrees nearest 0 on NA51089
     * are -337 and 982, both outside the whole-degree window. */
    zone[0].temp = "-337";
    CHECK(fabsf(nvt_get_temp() + 0.337f) < 0.001f);
    zone[0].temp = "-12";
    CHECK(nvt_get_temp() == -12.0f);

    /* A gap in the numbering: a driver that unregistered its zone. */
    fresh();
    zone[1].type = "nvt_thermal";
    zone[1].temp = "45828";
    CHECK(fabsf(nvt_get_temp() - 45.828f) < 0.001f);
    CHECK(mmio_calls == 0);

    /* Found by type, not by number. */
    fresh();
    zone[0].type = "cpu_thermal";
    zone[0].temp = "99000";
    zone[1].type = "nvt_thermal";
    zone[1].temp = "45828";
    CHECK(fabsf(nvt_get_temp() - 45.828f) < 0.001f);
    CHECK(mmio_calls == 0);

    /* The zone is there but will not read: no reading, and still no
     * register -- the ADC and the eFuse are the kernel's. */
    fresh();
    zone[0].type = "nvt_thermal";
    CHECK(isnan(nvt_get_temp()));
    CHECK(mmio_calls == 0);

    /* Some other zone only: the registers, as with none. */
    fresh();
    zone[0].type = "cpu_thermal";
    zone[0].temp = "99000";
    CHECK(near(nvt_get_temp(), 265, 228));

    /* /dev/mem withheld and no nvt_adc: some other zone if there is one,
     * else no reading. */
    fresh();
    mmio_fails = true;
    zone[0].type = "cpu_thermal";
    zone[0].temp = "66000";
    CHECK(nvt_get_temp() == 66.0f);
    zone[0].type = NULL;
    CHECK(isnan(nvt_get_temp()));

    /* A kernel without nvt_pinmux: the device tree names the die, and that
     * is enough to install the direct reader. */
    fresh();
    chip_generation = 0;
    dt_model = "NA51089";
    char name[128] = "";
    CHECK(novatek_detect_cpu(name));
    CHECK(!strcmp(name, "NA51089"));
    CHECK(chip_generation == CHIP_NA51089);
    hal_temperature = NULL;
    novatek_setup_hal();
    CHECK(hal_temperature == nvt_get_temp);

    /* A converter that never produces a sample is no reading. */
    fresh();
    M.code = 0;
    CHECK(isnan(nvt_get_temp()));

    /* NA51084 shares NA51089's formula and default; the SDK's default trim
     * is even named for the NT98528. */
    fresh();
    chip_generation = CHIP_NA51084;
    hal_temperature = NULL;
    novatek_setup_hal();
    CHECK(hal_temperature == nvt_get_temp);
    CHECK(near(hal_temperature(), 265, 228));
    M.fuse[6] = 0;
    nvt_trim = -1; /* another die, so another process */
    CHECK(near(nvt_get_temp(), 265, NVT_DEFAULT_TRIM));

    /* NA51055 has its own slope and default: 1.3 degC per code, trim 259. */
    fresh();
    chip_generation = CHIP_NA51055;
    hal_temperature = NULL;
    novatek_setup_hal();
    CHECK(hal_temperature == nvt_get_temp);
    CHECK(near55(hal_temperature(), 265, 228));
    M.fuse[6] = 0;
    nvt_trim = -1;
    CHECK(near55(nvt_get_temp(), 265, NA51055_DEFAULT_TRIM));
    CHECK(illegal == NULL);

    /* Their device-tree names set the ID as NA51089's does. */
    fresh();
    chip_generation = 0;
    dt_model = "NA51084";
    CHECK(novatek_detect_cpu(name));
    CHECK(chip_generation == CHIP_NA51084);
    fresh();
    chip_generation = 0;
    dt_model = "NA51055";
    CHECK(novatek_detect_cpu(name));
    CHECK(chip_generation == CHIP_NA51055);

    /* Every other die keeps the sysfs reader, if the host even has a
     * thermal zone: NA51090's peripherals sit at 0x2F0000000 (novatek.h),
     * NA51023's TOP at 0xC0000000, and NA51103 is in no SDK we have. */
    const int others[] = {CHIP_NA51090, CHIP_NA51103, CHIP_NA51023};
    for (unsigned i = 0; i < sizeof others / sizeof *others; i++) {
        fresh();
        chip_generation = others[i];
        hal_temperature = NULL;
        novatek_setup_hal();
        CHECK(hal_temperature != nvt_get_temp);
    }

    /* Their zone is read in whichever unit its driver uses. */
    fresh();
    zone[0].type = "thermal";
    zone[0].temp = "66";
    CHECK(novatek_get_temp() == 66.0f);
    zone[0].temp = "45000";
    CHECK(novatek_get_temp() == 45.0f);
    zone[0].type = NULL;
    CHECK(isnan(novatek_get_temp()));

    if (failures)
        fprintf(stderr, "novatek_test: %d failure(s)\n", failures);
    else
        printf("novatek_test: ok\n");
    return failures ? 1 : 0;
}
