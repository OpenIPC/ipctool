#include "hal/novatek.h"

#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include "hal/common.h"
#include "tools.h"

// TODO: /proc/nvt_info/nvt_pinmux/chip_id

static unsigned char sony_addrs[] = {0x34, 0};
static unsigned char ssens_addrs[] = {0x60, 0};
static unsigned char omni_addrs[] = {0x6c, 0};
static unsigned char onsemi_addrs[] = {0x20, 0};
static unsigned char gc_addrs[] = {0x6e, 0};

sensor_addr_t novatek_possible_i2c_addrs[] = {
    {SENSOR_SONY, sony_addrs},     {SENSOR_SMARTSENS, ssens_addrs},
    {SENSOR_ONSEMI, onsemi_addrs}, {SENSOR_OMNIVISION, omni_addrs},
    {SENSOR_GALAXYCORE, gc_addrs}, {0, NULL}};

bool novatek_detect_cpu(char *chip_name) {
    char buf[256];

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

void novatek_setup_hal() {
    possible_i2c_addrs = novatek_possible_i2c_addrs;
    if (!access("/sys/class/thermal/thermal_zone0/temp", R_OK))
        hal_temperature = novatek_get_temp;
#ifndef STANDALONE_LIBRARY
    hal_totalmem = novatek_totalmem;
#endif
}

enum CHIP_ID {
    CHIP_NA51055 = 0x4821, // NT98525, 128Kb L2, 5M@30
                           // NT98528, 256Kb L2, 4K@30
    CHIP_NA51084 = 0x5021,
    CHIP_NA51089 = 0x7021, // NT98562, 64Mb internal RAM
                           // NT98566, 128Mb internal RAM
    CHIP_NA51090 = 0xBC21
};

#define TOP_VERSION_REG_OFS 0xF0

/* na51000, na51089, na51068, na51000, na51090, na51055 */
#define IOADDR_GLOBAL_BASE (0xF0000000)
/* na51090 */
//#define IOADDR_GLOBAL_BASE (0x2F0000000)

/* na51000, na51089, na51000, na51090, na51090, na51055 */
#define IOADDR_TOP_REG_BASE (IOADDR_GLOBAL_BASE + 0x00010000)
/* na51068 */
//#define IOADDR_TOP_REG_BASE (IOADDR_GLOBAL_BASE + 0x0E030000)

static uint32_t nvt_get_chip_id() {
    uint32_t reg;
    unsigned int chip_id = 0;

    if (mem_reg(IOADDR_TOP_REG_BASE + TOP_VERSION_REG_OFS, (uint32_t *)&reg,
                OP_READ)) {
        chip_id = (reg >> 16) & 0xFFFF;
    }

    return chip_id;
}
