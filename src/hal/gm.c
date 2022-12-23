#include "hal/gm.h"

#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include "hal/common.h"
#include "tools.h"

static unsigned char sony_addrs[] = {0x34, 0};
static unsigned char ssens_addrs[] = {0x60, 0};
static unsigned char omni_addrs[] = {0x6c, 0};
static unsigned char onsemi_addrs[] = {0x20, 0};
static unsigned char gc_addrs[] = {0x6e, 0};

sensor_addr_t gm_possible_i2c_addrs[] = {
    {SENSOR_SONY, sony_addrs},     {SENSOR_SMARTSENS, ssens_addrs},
    {SENSOR_ONSEMI, onsemi_addrs}, {SENSOR_OMNIVISION, omni_addrs},
    {SENSOR_GALAXYCORE, gc_addrs}, {0, NULL}};

bool gm_detect_cpu(char *chip_name) {

    char buf[256];
    char chip[256];

    if (!get_regex_line_from_file("/proc/pmu/chipver", "([0-9]+)", buf,
                                  sizeof(buf)))
        return false;
    sprintf(chip_name, "GM%.4s", buf);
    return true;
}

static unsigned long gm_media_mem() {
    char buf[256];
    // Fixme: this seems to be currently allocated size
    if (!get_regex_line_from_file("/proc/frammap/ddr_info",
                                  "size:.([0-9A-Fx]+)", buf, sizeof(buf)))
        return 0;
    return strtoul(buf, NULL, 16) / 1024;
}

unsigned long gm_totalmem(unsigned long *media_mem) {
    *media_mem = gm_media_mem();
    return kernel_mem();
}

float gm_get_temp() {
    float ret = -237.0;
    char buf[16];
    if (get_regex_line_from_file("/sys/class/thermal/thermal_zone0/temp",
                                 "(.+)", buf, sizeof(buf))) {
        ret = strtof(buf, NULL);
    }
    return ret;
}

void gm_setup_hal() {
    possible_i2c_addrs = gm_possible_i2c_addrs;
    if (!access("/sys/class/thermal/thermal_zone0/temp", R_OK))
        hal_temperature = gm_get_temp;
#ifndef STANDALONE_LIBRARY
    hal_totalmem = gm_totalmem;
#endif
}
