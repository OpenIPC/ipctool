#include "hal/gm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include "hal/common.h"
#include "tools.h"

static unsigned char sony_addrs[] = {0x34, 0};
static unsigned char ssens_addrs[] = {0x60, 0};
/* 0x42: the OV77xx, on GM8126 cameras */
static unsigned char omni_addrs[] = {0x6c, 0x42, 0};
static unsigned char onsemi_addrs[] = {0x20, 0};
static unsigned char gc_addrs[] = {0x6e, 0};

static sensor_addr_t gm_possible_i2c_addrs[] = {
    {SENSOR_SONY, sony_addrs},     {SENSOR_SMARTSENS, ssens_addrs},
    {SENSOR_ONSEMI, onsemi_addrs}, {SENSOR_OMNIVISION, omni_addrs},
    {SENSOR_GALAXYCORE, gc_addrs}, {0, NULL}};

bool gm_detect_cpu(char *chip_name) {

    char buf[256];
    char chip[256];

    if (!line_from_file("/proc/pmu/chipver", "([0-9]+)", buf, sizeof(buf)))
        return false;
    sprintf(chip_name, "GM%.4s", buf);
    return true;
}

/* A mainline kernel: no /proc/pmu, the device tree names the chip. */
bool gm_dt_detect_cpu(char *chip_name) {
    static const char prefix[] = "grainmedia,gm";
    char buf[256];

    FILE *f = fopen("/proc/device-tree/compatible", "r");
    if (!f)
        return false;
    size_t len = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[len] = 0;

    /* NUL-separated strings, the board's before the chip's */
    for (char *s = buf; s < buf + len; s += strlen(s) + 1) {
        if (!strncmp(s, prefix, sizeof(prefix) - 1)) {
            sprintf(chip_name, "GM%.4s", s + sizeof(prefix) - 1);
            return true;
        }
    }
    return false;
}

static unsigned long gm_media_mem() {
    char buf[256];
    // Fixme: this seems to be currently allocated size
    if (!line_from_file("/proc/frammap/ddr_info", "size:.([0-9A-Fx]+)", buf,
                        sizeof(buf)))
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
    if (line_from_file("/sys/class/thermal/thermal_zone0/temp", "(.+)", buf,
                       sizeof(buf))) {
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
