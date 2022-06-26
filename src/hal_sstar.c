#include "hal_sstar.h"

#include <stdlib.h>
#include <string.h>

#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "chipid.h"
#include "hal_common.h"
#include "tools.h"

static unsigned char sony_addrs[] = {0x34, 0};
static unsigned char ssens_addrs[] = {0x60, 0};
static unsigned char omni_addrs[] = {0x6c, 0};
static unsigned char onsemi_addrs[] = {0x20, 0};
static unsigned char gc_addrs[] = {0x6e, 0};

sensor_addr_t sstar_possible_i2c_addrs[] = {
    {SENSOR_SONY, sony_addrs},     {SENSOR_SMARTSENS, ssens_addrs},
    {SENSOR_ONSEMI, onsemi_addrs}, {SENSOR_OMNIVISION, omni_addrs},
    {SENSOR_GALAXYCORE, gc_addrs}, {0, NULL}};

bool sstar_detect_cpu() {
    uint32_t val;

    if (!strcmp(chip_manufacturer, VENDOR_MSTAR)) {
        if (mem_reg(0x1f2025a4, (uint32_t *)&val, OP_READ)) {
            switch (val & 0xF000) {
            case 0x6000:
                strcpy(chip_name, "MSC313E");
                break;
            case 0x7000:
                strcpy(chip_name, "MSC316DC");
                break;
            case 0x8000:
                strcpy(chip_name, "MSC318");
                break;
            }
        return true;
        }
    }

    if (mem_reg(0x1f003c00, &val, OP_READ)) {
        snprintf(chip_name, sizeof(chip_name), "id %#x", val);
        chip_generation = val;
        return true;
    }
    return false;
}

static unsigned long sstar_media_mem() {
    char buf[256];

    if (!get_regex_line_from_file("/proc/cmdline",
                                  "mma_heap=.+sz=(0x[0-9A-Fa-f]+)", buf,
                                  sizeof(buf)))
        return 0;
    return strtoul(buf, NULL, 16) / 1024;
}

unsigned long sstar_totalmem(unsigned long *media_mem) {
    *media_mem = sstar_media_mem();
    return *media_mem + kernel_mem();
}

int sstar_open_sensor_fd() { return universal_open_sensor_fd("/dev/i2c-1"); }

static int sstar_i2c_write(int fd, unsigned char slave_addr,
                           unsigned char *reg_addr, unsigned char reg_width) {
    unsigned int data_size = 0;

    data_size = reg_width * sizeof(unsigned char);
    return 0;
}

static void sstar_hal_cleanup() {}

float sstar_get_temp() {
    float ret = -237.0;
    char buf[16];
    if (get_regex_line_from_file("/sys/class/mstar/msys/TEMP_R",
                                 "Temperature\\s+(.+)", buf, sizeof(buf))) {
        ret = strtof(buf, NULL);
    }
    return ret;
}

void setup_hal_sstar() {
    open_i2c_sensor_fd = sstar_open_sensor_fd;
    possible_i2c_addrs = sstar_possible_i2c_addrs;
    hal_cleanup = sstar_hal_cleanup;
    if (!access("/sys/class/mstar/msys/TEMP_R", R_OK))
        hal_temperature = sstar_get_temp;
}
