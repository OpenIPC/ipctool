#include "hal_ingenic.h"

#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include "chipid.h"
#include "hal_common.h"
#include "tools.h"

static unsigned char sony_addrs[] = {0x34, 0};
static unsigned char ssens_addrs[] = {0x60, 0};
static unsigned char omni_addrs[] = {0x6c, 0};
static unsigned char onsemi_addrs[] = {0x20, 0};
static unsigned char gc_addrs[] = {0x6e, 0};

sensor_addr_t ingenic_possible_i2c_addrs[] = {
    {SENSOR_SONY, sony_addrs},     {SENSOR_SMARTSENS, ssens_addrs},
    {SENSOR_ONSEMI, onsemi_addrs}, {SENSOR_OMNIVISION, omni_addrs},
    {SENSOR_GALAXYCORE, gc_addrs}, {0, NULL}};

bool ingenic_detect_cpu() {

    // chipid (serial number?)
    // 0x13540200: 0xEBD9102F
    // 0x13540204: 0x3A922801
    // 0x13540208: 0x00001456
    // result: 2f10d9eb0128923a56140000

    char buf[256];

    unsigned int chip_id = 0;

    if (!get_regex_line_from_file("/proc/cpuinfo",
                                  "system.type.+: ([a-zA-Z0-9-]+)", buf,
                                  sizeof(buf)))
        return false;

    strcpy(chip_name, buf);
    strcpy(chip_manufacturer, VENDOR_INGENIC);
    return true;
}

static unsigned long ingenic_media_mem() {
    char buf[256];

    if (!get_regex_line_from_file("/proc/cmdline", "rmem=([0-9x]+)", buf,
                                  sizeof(buf)))
        return 0;
    return strtoul(buf, NULL, 10);
}

unsigned long ingenic_totalmem(unsigned long *media_mem) {
    *media_mem = ingenic_media_mem();
    return kernel_mem();
}

float ingenic_get_temp() {
    float ret = -237.0;
    char buf[16];
    if (get_regex_line_from_file("/sys/class/thermal/thermal_zone0/temp",
                                 "(.+)", buf, sizeof(buf))) {
        ret = strtof(buf, NULL);
    }
    return ret;
}

void setup_hal_ingenic() {
    disable_printk();
    possible_i2c_addrs = ingenic_possible_i2c_addrs;
    if (!access("/sys/class/thermal/thermal_zone0/temp", R_OK))
        hal_temperature = ingenic_get_temp;
}