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
    uint16_t reg;
    unsigned int chip_id = 0;
    char buf[8];
    char *endptr;

    if (line_from_file("/proc/nvt_info/nvt_pinmux/chip_id", "(.+)", buf,
                       sizeof(buf))) {

        reg = (uint16_t)strtol(buf, &endptr, 16);
        switch (reg) {
        case 0x5021:
            strcpy(chip_name, "NT98562");
            return true;
        case 0x7021:
            strcpy(chip_name, "NT98566");
            return true;
        case 0x8B20:
            strcpy(chip_name, "NT98332G");
            return true;
        }
    }

    // if (mem_reg(IOADDR_TOP_REG_BASE + TOP_VERSION_REG_OFS, (uint32_t *)&reg,
    //             OP_READ)) {
    //     chip_id = (reg >> 16) & 0xFFFF;
    // }
    // printf("reg %x\n", chip_id);
    return false;
}

bool novatek_detect_cpu(char *chip_name) {
    char buf[256];

    if(nvt_get_chip_id())
        return true;

    if (!line_from_file("/proc/device-tree/model", "Novatek ([A-Z]+[0-9]+)",
                        buf, sizeof(buf)))
        return false;

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
    if (!strncmp(chip_name, "NA51068", 7) ||
        !strncmp(chip_name, "NA51103", 7) || !strncmp(chip_name, "NA51090", 7))
        i2c_adapter_nr = 1;
    char adapter_name[FILENAME_MAX];

    snprintf(adapter_name, sizeof(adapter_name), "/dev/i2c-%d", i2c_adapter_nr);

    return universal_open_sensor_fd(adapter_name);
}

void novatek_setup_hal() {
    possible_i2c_addrs = novatek_possible_i2c_addrs;
    i2c_change_addr = i2c_changenshift_addr;
    open_i2c_sensor_fd = novatek_open_i2c_fd;
    if (!access("/sys/class/thermal/thermal_zone0/temp", R_OK))
        hal_temperature = novatek_get_temp;
#ifndef STANDALONE_LIBRARY
    hal_totalmem = novatek_totalmem;
#endif
}
