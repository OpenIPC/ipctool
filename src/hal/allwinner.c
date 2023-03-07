#include "hal/allwinner.h"

#include <arpa/inet.h>
#include <dirent.h>
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

static sensor_addr_t allwinner_possible_i2c_addrs[] = {
    {SENSOR_SONY, sony_addrs},     {SENSOR_SMARTSENS, ssens_addrs},
    {SENSOR_ONSEMI, onsemi_addrs}, {SENSOR_OMNIVISION, omni_addrs},
    {SENSOR_GALAXYCORE, gc_addrs}, {0, NULL}};

bool allwinner_detect_cpu(char *chip_name) {
    char buf[256];

    if (!line_from_file("/proc/device-tree/compatible", "allwinner,(sun.*)",
                        buf, sizeof(buf)))
        return false;
    strcpy(chip_name, buf);
    if (!strcmp(chip_name, "sun8iw8p1"))
        strcpy(chip_name, "s3");
    if (!strcmp(chip_name, "sun8iw19p1"))
        strcpy(chip_name, "V83x");
    return true;
}

unsigned long allwinner_totalmem(unsigned long *media_mem) {
    return kernel_mem();
}

float allwinner_get_temp() {
    float ret = -237.0;
    char buf[16];
    if (line_from_file("/sys/class/thermal/thermal_zone0/temp", "(.+)", buf,
                       sizeof(buf))) {
        ret = strtof(buf, NULL);
    }
    return (float)ret / 1000;
}

static int i2c1_open_sensor_fd() {
    return universal_open_sensor_fd("/dev/i2c-1");
}

void allwinner_setup_hal() {
    possible_i2c_addrs = allwinner_possible_i2c_addrs;
    open_i2c_sensor_fd = i2c1_open_sensor_fd;
    if (!access("/sys/class/thermal/thermal_zone0/temp", R_OK))
        hal_temperature = allwinner_get_temp;
#ifndef STANDALONE_LIBRARY
    hal_totalmem = allwinner_totalmem;
#endif
}
