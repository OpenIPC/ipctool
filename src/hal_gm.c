#include "hal_gm.h"

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

sensor_addr_t gm_possible_i2c_addrs[] = {
    {SENSOR_SONY, sony_addrs},     {SENSOR_SMARTSENS, ssens_addrs},
    {SENSOR_ONSEMI, onsemi_addrs}, {SENSOR_OMNIVISION, omni_addrs},
    {SENSOR_GALAXYCORE, gc_addrs}, {0, NULL}};

bool gm_detect_cpu() {
    
    char buf[256];
    char chip[256];

    if (!get_regex_line_from_file("/proc/pmu/chipver",
                                  "([0-9]+)", buf, sizeof(buf)))
        return false;
    snprintf(chip, sizeof(chip), "GM%.4s", buf);
    strncpy(chip_name, chip, sizeof(chip_name) - 1);
    return true;
}

static unsigned long gm_media_mem() {
    char buf[256];
    // Fixme: this seems to be currently allocated size
    if (!get_regex_line_from_file("/proc/frammap/ddr_info",
                                  "size:.([0-9A-Fx]+)", buf,
                                  sizeof(buf)))
        return 0;
    return strtoul(buf, NULL, 16) / 1024;
}

unsigned long gm_totalmem(unsigned long *media_mem) {
    *media_mem = gm_media_mem();
    return kernel_mem();
}

int gm_open_sensor_fd() { return universal_open_sensor_fd("/dev/i2c-0"); }

static void gm_hal_cleanup() {}

float gm_get_temp() {
    float ret = -237.0;
    char buf[16];
    if (get_regex_line_from_file("/sys/class/thermal/thermal_zone0/temp",
                                 "(.+)", buf, sizeof(buf))) {
        ret = strtof(buf, NULL);
    }
    return ret;
}

void setup_hal_gm() {
    open_i2c_sensor_fd = gm_open_sensor_fd;
    close_sensor_fd = universal_close_sensor_fd;
    i2c_change_addr = universal_sensor_i2c_change_addr;
    i2c_read_register = universal_sensor_read_register;
    i2c_write_register = universal_sensor_write_register;
    possible_i2c_addrs = gm_possible_i2c_addrs;
    hal_cleanup = gm_hal_cleanup;
    if (!access("/sys/class/thermal/thermal_zone0/temp", R_OK))
        hal_temperature = gm_get_temp;
}
