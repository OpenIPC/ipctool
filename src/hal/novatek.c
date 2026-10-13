#include "hal/novatek.h"

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

    if (!line_from_file("/proc/nvt_info/nvt_pinmux/chip_id", "(.+)", buf,
                        sizeof(buf)))
        return false;

    /* The TOP block's chip-ID word, which is what tells the pad-mux table and
     * the GPIO controller which SoC this is. Set for every ID the kernel hands
     * over, named or not: a part with no table gets no table, which
     * padmux_ops() says rather than guessing one. */
    chip_generation = (uint16_t)strtol(buf, NULL, 16);

    /* The ID names the die, not the part: NT98562 and NT98566 are both
     * NA51089 and differ only in an eFuse package word whose decoding is not
     * in the SDK, so the two read alike here. The dies with no name below
     * (NA51055, NA51090) are named by the device tree. */
    const char *name;
    switch (chip_generation) {
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

#if defined(mips) || defined(__mips__) || defined(__mips)
static void nvt_refine_na51023(char *chip_name) {
    char buf[16];

    /* The device-tree model names a die shared by more than one part.
     * Parse the factory declaration as data; never source it. Missing or
     * different profiles retain the NA51023 identity. */
    chip_generation = CHIP_NA51023;
    if (line_from_file("/etc/profile_prjcfg",
                       "^export CHIP=\"(98513)\"[[:space:]]*$", buf,
                       sizeof(buf)))
        strcpy(chip_name, "NT98513");
}

bool novatek_detect_mips_cpu(char *chip_name) {
    char buf[16];

    /* Only this MIPS model has a verified identity and rejecting HAL.
     * Other Novatek models and the ARM chip-ID proc ABI must not select
     * the existing ARM sensor/HDAL backend through MIPS dispatch. */
    if (!line_from_file("/proc/device-tree/model",
                       "^Novatek (NA51023)([[:space:]]|$)", buf, sizeof(buf)))
        return false;
    strcpy(chip_name, buf);
    nvt_refine_na51023(chip_name);
    return true;
}
#endif

bool novatek_detect_cpu(char *chip_name) {
    char buf[256];

    if(nvt_get_chip_id())
        return true;

    if (!line_from_file("/proc/device-tree/model",
#if defined(mips) || defined(__mips__) || defined(__mips)
                        "^Novatek ([A-Z]+[0-9]+)([[:space:]]|$)",
#else
                        /* Keep the existing ARM/AArch64 parser unchanged.
                         * The new MIPS route has its own strict detector. */
                        "Novatek ([A-Z]+[0-9]+)",
#endif
                        buf, sizeof(buf))) {
        /* NA51055 has a pad table and no part name the ID can give; with
         * no device-tree model either, the die is the name. */
        if (chip_generation != CHIP_NA51055)
            return false;
        strcpy(buf, "NA51055");
    }

    strcpy(chip_name, buf);

#if defined(mips) || defined(__mips__) || defined(__mips)
    if (!strcmp(buf, "NA51023"))
        nvt_refine_na51023(chip_name);
#endif

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

float novatek_get_temp() {
    float ret = -237.0;
    char buf[16];
    if (line_from_file("/sys/class/thermal/thermal_zone0/temp", "(.+)", buf,
                       sizeof(buf))) {
        ret = strtof(buf, NULL);
    }
    return ret;
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

#if defined(mips) || defined(__mips__) || defined(__mips)
static int novatek_no_sensor_bus(int adapter_nr) {
    (void)adapter_nr;
    return -1;
}
#endif

void novatek_setup_hal() {
#if defined(mips) || defined(__mips__) || defined(__mips)
    /* This MIPS generation delegates media to uITRON. The ARM HDAL memory
     * report, sensor I2C bus and register tables below are not its ABI. */
    if (chip_generation == CHIP_NA51023) {
        possible_i2c_addrs = NULL;
        open_i2c_sensor_fd = novatek_no_sensor_bus;
        open_spi_sensor_fd = NULL;
        return;
    }
#endif
    possible_i2c_addrs = novatek_possible_i2c_addrs;
    i2c_change_addr = i2c_changenshift_addr;
    open_i2c_sensor_fd = novatek_open_i2c_fd;
    if (!access("/sys/class/thermal/thermal_zone0/temp", R_OK))
        hal_temperature = novatek_get_temp;
#ifndef STANDALONE_LIBRARY
    hal_totalmem = novatek_totalmem;
#endif
}
