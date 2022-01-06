#include "hal_novatek.h"

#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include "chipid.h"
#include "hal_common.h"
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

bool novatek_detect_cpu() {
    char buf[256];

    if (!get_regex_line_from_file("/proc/device-tree/model",
                                  "Novatek ([A-Z]+[0-9]+)", buf, sizeof(buf)))
        return false;
    strncpy(chip_id, buf, sizeof(chip_id) - 1);
    return true;
}

static unsigned long novatek_media_mem() {
    char buf[256];

    if (!get_regex_line_from_file("/proc/hdal/comm/info",
                                  "DDR[0-9]:.+size = ([0-9A-Fx]+)", buf,
                                  sizeof(buf)))
        return 0;
    return strtoul(buf, NULL, 16) / 1024;
}

unsigned long novatek_totalmem(unsigned long *media_mem) {
    *media_mem = novatek_media_mem();
    return *media_mem + kernel_mem();
}

int novatek_open_sensor_fd() { return universal_open_sensor_fd("/dev/i2c-0"); }

static void novatek_hal_cleanup() {}

float novatek_get_temp() {
    float ret = -237.0;
    char buf[16];
    if (get_regex_line_from_file("/sys/class/thermal/thermal_zone0/temp",
                                 "(.+)", buf, sizeof(buf))) {
        ret = strtof(buf, NULL);
    }
    return ret;
}

void setup_hal_novatek() {
    open_sensor_fd = novatek_open_sensor_fd;
    close_sensor_fd = universal_close_sensor_fd;
    sensor_i2c_change_addr = universal_sensor_i2c_change_addr;
    sensor_read_register = universal_sensor_read_register;
    sensor_write_register = universal_sensor_write_register;
    possible_i2c_addrs = novatek_possible_i2c_addrs;
    hal_cleanup = novatek_hal_cleanup;
    if (!access("/sys/class/thermal/thermal_zone0/temp", R_OK))
        hal_temperature = novatek_get_temp;
}
