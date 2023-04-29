#include "hal/rockchip.h"

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

static sensor_addr_t rockchip_possible_i2c_addrs[] = {
    {SENSOR_SONY, sony_addrs},     {SENSOR_SMARTSENS, ssens_addrs},
    {SENSOR_ONSEMI, onsemi_addrs}, {SENSOR_OMNIVISION, omni_addrs},
    {SENSOR_GALAXYCORE, gc_addrs}, {0, NULL}};

bool rockchip_detect_cpu(char *chip_name) {
    char buf[256];

    if (!line_from_file("/proc/device-tree/compatible",
                        "rockchip,(r[kv][0-9]+)", buf, sizeof(buf)))
        return false;
    strcpy(chip_name, buf);
    return true;
}

static unsigned get_size_from_proc(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (fp == NULL)
        return 0;

    unsigned value = 0;
    int ret = fread(&value, 1, sizeof(value), fp);
    value = ntohl(value);

    fclose(fp);
    return value;
}

static unsigned long rockchip_media_mem() {
    unsigned long total = 0;
    char buf[512] = {0};

    const char *proc_dir = "/proc/device-tree/reserved-memory";
    DIR *dir = opendir(proc_dir);
    if (!dir)
        return false;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (*entry->d_name && *entry->d_name != '.' &&
            entry->d_type == DT_DIR) {
            snprintf(buf, sizeof(buf), "%s/%s/size", proc_dir, entry->d_name);
            unsigned size = get_size_from_proc(buf);
            total += size;
        }
    }

    closedir(dir);
    return total / 1024;
}

unsigned long rockchip_totalmem(unsigned long *media_mem) {
    *media_mem = rockchip_media_mem();
    return *media_mem + kernel_mem();
}

float rockchip_get_temp() {
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

void rockchip_setup_hal() {
    possible_i2c_addrs = rockchip_possible_i2c_addrs;
    open_i2c_sensor_fd = i2c1_open_sensor_fd;
    if (!access("/sys/class/thermal/thermal_zone0/temp", R_OK))
        hal_temperature = rockchip_get_temp;
#ifndef STANDALONE_LIBRARY
    hal_totalmem = rockchip_totalmem;
#endif
}
