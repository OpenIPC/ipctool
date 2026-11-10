#include "hal/novatek.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include "chipid.h"
#include "hal/common.h"
#include "tools.h"

static unsigned char sony_addrs[] = {0x34, 0};
static unsigned char ssens_addrs[] = {0x60, 0};
static unsigned char omni_addrs[] = {0x6c, 0};
static unsigned char onsemi_addrs[] = {0x20, 0};
static unsigned char gc_addrs[] = {0x6e, 0};
static unsigned char tp_addrs[] = {0x88, 0};

static sensor_addr_t novatek_possible_i2c_addrs[] = {
    {SENSOR_SONY, sony_addrs},     {SENSOR_SMARTSENS, ssens_addrs},
    {SENSOR_ONSEMI, onsemi_addrs}, {SENSOR_OMNIVISION, omni_addrs},
    {SENSOR_GALAXYCORE, gc_addrs}, {SENSOR_TECHPOINT, tp_addrs},
    {0, NULL}};

static bool nvt_get_chip_id() {
    char buf[8];

    /* The TOP block's chip-ID word, which is what tells the pad-mux table and
     * the GPIO controller which SoC this is. Set for every ID the kernel hands
     * over, named or not: a part with no table gets no table, which
     * padmux_ops() says rather than guessing one. */
    if (line_from_file("/proc/nvt_info/nvt_pinmux/chip_id", "(.+)", buf,
                       sizeof(buf))) {
        chip_generation = (uint16_t)strtol(buf, NULL, 16);
    } else {
#if defined(__mips__)
        /* NA51023's 4.1 kernel predates nvt_pinmux. Its TOP block is
         * NA51089's at 0xC0000000 instead of 0xF0000000, and the vendor
         * firmware's drv_init() aborts unless TOP+0xF0 [31:16] is 0x6E19.
         * Only that value is taken: this is a refinement, the device-tree
         * model below still names the die on a kernel that withholds
         * /dev/mem. */
        uint32_t reg;
        if (!mem_reg(0xC0010000 + TOP_VERSION_REG_OFS, &reg, OP_READ) ||
            (reg >> 16) != CHIP_NA51023)
            return false;
        chip_generation = CHIP_NA51023;
#else
        return false;
#endif
    }

    /* The ID names the die, not the part: NT98562 and NT98566 are both
     * NA51089 and differ only in an eFuse package word whose decoding is not
     * in the SDK, so the two read alike here. The dies with no name below
     * (NA51055, NA51090) are named by the device tree. */
    const char *name;
    switch (chip_generation) {
    case CHIP_NA51023:
        /* One ID for the NT96672 dashcam part and the NT9851x camera parts;
         * nothing in the BSP tells them apart, so the die is the name. */
        name = "NA51023";
        break;
    case CHIP_NA51084:
        /* u-boot's NA51084 code drives the "_528_" clock registers and allows
         * 1200 MHz only on an NT98529, told apart by OTP. */
        name = "NT98528";
        break;
    case CHIP_NA51089:
        name = "NT98566";
        break;
    case CHIP_NA51103:
        name = "NT98332G";
        break;
    default:
        return false;
    }
    strcpy(chip_name, name);
    return true;
}

bool novatek_detect_cpu(char *chip_name) {
    char buf[256];

    if(nvt_get_chip_id())
        return true;

    if (!line_from_file("/proc/device-tree/model", "Novatek ([A-Z]+[0-9]+)",
                        buf, sizeof(buf))) {
        /* NA51055 has a pad table and no part name the ID can give; with
         * no device-tree model either, the die is the name. */
        if (chip_generation != CHIP_NA51055)
            return false;
        strcpy(buf, "NA51055");
    }

    strcpy(chip_name, buf);

    return true;
}

static unsigned long novatek_media_mem() {
    char buf[256];

    if (!line_from_file("/proc/hdal/comm/info",
                        "DDR[0-9]:.+size = ([0-9A-Fx]+)", buf, sizeof(buf)))
        return 0;
    return strtoul(buf, NULL, 16) / 1024;
}

unsigned long novatek_totalmem(unsigned long *media_mem) {
    *media_mem = novatek_media_mem();
    return *media_mem + kernel_mem();
}

/* The vendor nvt_adc driver's thermal zone. It reports whole degrees, not the
 * millidegrees the sysfs ABI promises, so the value is taken as it stands. */
float novatek_get_temp() {
    char buf[16];
    if (line_from_file("/sys/class/thermal/thermal_zone0/temp", "(.+)", buf,
                       sizeof(buf)))
        return strtof(buf, NULL);
    return NAN;
}

/* NA51089's die thermometer is channel 3 of the SAR ADC, averaged by the ADC
 * into its own register. OpenIPC's kernel builds no nvt_adc driver, so the ADC
 * is found gated and idle, and is started here the way the RTOS side's
 * thermal_channel_init() does: clock on, channel 3 continuous, converter on.
 * Both are left running, as the vendor driver leaves them; turning a clock off
 * is how a probe takes a peripheral away from its owner
 * (OpenIPC/firmware#2439). */
#define NA51089_CG_CLK_EN1 0xF0020074
#define NA51089_CG_ADC (1u << 13)
#define NA51089_ADC_CTRL 0xF0260000
#define NA51089_ADC_AIN3_MODE (1u << 3)
#define NA51089_ADC_EN (1u << 23)
#define NA51089_ADC_THERMAL 0xF0260040 /* bits [24:16], the average */

/* The per-die calibration is eFuse row 6 (EFUSE_THERMAL_TRIM_DATA). Its read
 * sequence lives only in the SDK's binary otp_protected.o_shipped; this is
 * that blob's efuse_open(0, 0, 0) / efuse_read() / efuse_close(), read mode
 * throughout, so nothing here can program a fuse. */
#define NA51089_EFUSE 0xF0660000
#define EFUSE_MODE (NA51089_EFUSE + 0x00)
#define EFUSE_ROW (NA51089_EFUSE + 0x04)
#define EFUSE_DATA (NA51089_EFUSE + 0x08)
#define EFUSE_CTRL (NA51089_EFUSE + 0x20) /* bit 0 open, bit 4 read */
#define EFUSE_STS (NA51089_EFUSE + 0x30)  /* bit 0 opened, bit 1 read done */
#define NA51089_THERMAL_TRIM_ROW 6
/* nvt_adc's DEFAULT_THERMAL_528_LEVEL, for a die with no trim fused */
#define NA51089_DEFAULT_TRIM 231

static bool nvt_wait(uint32_t addr, uint32_t mask, uint32_t want) {
    uint32_t val;
    for (int i = 0; i < 1000; i++) {
        if (!mem_reg(addr, &val, OP_READ))
            return false;
        if ((val & mask) == want)
            return true;
    }
    return false;
}

static bool nvt_efuse_row(uint32_t row, uint32_t *data) {
    uint32_t val;
    bool ok = false;

    /* The kernel serialises this block with a semaphore we cannot take. If
     * it is open, someone is mid-sequence: leave it to them. */
    if (!mem_reg(EFUSE_CTRL, &val, OP_READ) || (val & 1))
        return false;
    val |= 1;
    if (!mem_reg(EFUSE_CTRL, &val, OP_WRITE))
        return false;
    if (!nvt_wait(EFUSE_STS, 1, 1))
        goto close;

    val = 0;
    mem_reg(EFUSE_MODE, &val, OP_WRITE);
    mem_reg(EFUSE_ROW, &row, OP_WRITE);
    if (!mem_reg(EFUSE_CTRL, &val, OP_READ))
        goto close;
    val |= 0x10;
    mem_reg(EFUSE_CTRL, &val, OP_WRITE);
    if (!nvt_wait(EFUSE_CTRL, 0x10, 0) || !nvt_wait(EFUSE_STS, 2, 2))
        goto close;
    if (mem_reg(EFUSE_STS, &val, OP_READ))
        mem_reg(EFUSE_STS, &val, OP_WRITE); /* write-one-to-clear */
    ok = mem_reg(EFUSE_DATA, data, OP_READ);

close:
    val = 0;
    mem_reg(EFUSE_ROW, &val, OP_WRITE);
    if (mem_reg(EFUSE_CTRL, &val, OP_READ)) {
        val &= ~1u;
        mem_reg(EFUSE_CTRL, &val, OP_WRITE);
        nvt_wait(EFUSE_STS, 1, 0);
    }
    return ok;
}

/* A row holds two copies, as efuse_readParamOps() reads them: bits [15:0]
 * first, then [31:16]. In each, bit 15 says programmed and bit 14 says
 * invalidated; the trim is the low 14 bits of the first good one. */
static int na51089_thermal_trim() {
    uint32_t row;
    if (nvt_efuse_row(NA51089_THERMAL_TRIM_ROW, &row))
        for (int half = 0; half < 2; half++) {
            const uint32_t w = (row >> (16 * half)) & 0xffff;
            if ((w & 0xc000) == 0x8000)
                return w & 0x3fff;
        }
    return NA51089_DEFAULT_TRIM;
}

static float na51089_get_temp() {
    uint32_t val;

    if (!mem_reg(NA51089_CG_CLK_EN1, &val, OP_READ))
        return NAN;
    if (!(val & NA51089_CG_ADC)) {
        val |= NA51089_CG_ADC;
        if (!mem_reg(NA51089_CG_CLK_EN1, &val, OP_WRITE))
            return NAN;
    }

    /* adc_en alone converts nothing: channel 3 must be in continuous mode. */
    const uint32_t on = NA51089_ADC_AIN3_MODE | NA51089_ADC_EN;
    if (!mem_reg(NA51089_ADC_CTRL, &val, OP_READ))
        return NAN;
    if ((val & on) != on) {
        val |= on;
        if (!mem_reg(NA51089_ADC_CTRL, &val, OP_WRITE))
            return NAN;
    }

    /* The average reads 0 until the first conversion, well under 1 ms. */
    uint32_t code = 0;
    for (int i = 0; i < 50 && !code; i++) {
        if (!mem_reg(NA51089_ADC_THERMAL, &val, OP_READ))
            return NAN;
        code = (val >> 16) & 0x1ff;
        if (!code)
            usleep(1000);
    }
    if (!code)
        return NAN;

    /* nvt_adc's formula for every die but NA51055: 1.319 degC per code */
    return ((int)code - na51089_thermal_trim()) * 1.319f + 30;
}

static int novatek_open_i2c_fd(int i2c_adapter_nr) {
    if (i2c_adapter_nr<0 || i2c_adapter_nr>4)
        i2c_adapter_nr=0;
    /* NA51103 by its ID: the ID path names it NT98332G, which a name test
     * for "NA51103" never matched. */
    if (chip_generation == CHIP_NA51103 || !strncmp(chip_name, "NA51068", 7) ||
        !strncmp(chip_name, "NA51090", 7))
        i2c_adapter_nr = 1;
    char adapter_name[FILENAME_MAX];

    snprintf(adapter_name, sizeof(adapter_name), "/dev/i2c-%d", i2c_adapter_nr);

    return universal_open_sensor_fd(adapter_name);
}

void novatek_setup_hal() {
    possible_i2c_addrs = novatek_possible_i2c_addrs;
    i2c_change_addr = i2c_changenshift_addr;
    open_i2c_sensor_fd = novatek_open_i2c_fd;
    if (chip_generation == CHIP_NA51089)
        hal_temperature = na51089_get_temp;
    else if (!access("/sys/class/thermal/thermal_zone0/temp", R_OK))
        hal_temperature = novatek_get_temp;
#ifndef STANDALONE_LIBRARY
    hal_totalmem = novatek_totalmem;
#endif
}
