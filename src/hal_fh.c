#include "hal_fh.h"

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

sensor_addr_t fh_possible_i2c_addrs[] = {
    {SENSOR_SONY, sony_addrs},     {SENSOR_SMARTSENS, ssens_addrs},
    {SENSOR_ONSEMI, onsemi_addrs}, {SENSOR_OMNIVISION, omni_addrs},
    {SENSOR_GALAXYCORE, gc_addrs}, {0, NULL}};

bool fh_detect_cpu() {

    char buf[256];
    uint32_t sysctrl, ipver, reg;
    unsigned int chip_id = 0;

    if (!get_regex_line_from_file(
            "/proc/cpuinfo", "Hardware.+: ([a-zA-Z0-9-]+)", buf, sizeof(buf)))
        return false;

    mem_reg(REG_PMU_SYS_CTRL, (uint32_t *)&sysctrl, OP_READ);
    mem_reg(REG_PMU_IP_VER, (uint32_t *)&ipver, OP_READ);
    if (mem_reg(REG_PMU_CHIP_ID, (uint32_t *)&reg, OP_READ)) {
        switch (reg) {
        case 0x19112201:
            switch (sysctrl & 0xffffff) {
            case 0x00000001:
                strcpy(chip_name, "FH8852V200");
                break;
            case 0x00100001:
                strcpy(chip_name, "FH8856V200");
                break;
            case 0x00410001:
                strcpy(chip_name, "FH8858V200");
                break;
            case 0x00000002:
                strcpy(chip_name, "FH8852V210");
                break;
            case 0x00100002:
                strcpy(chip_name, "FH8856V210");
                break;
            case 0x00410002:
                strcpy(chip_name, "FH8858V210");
                break;
            case 0x00200001:
                strcpy(chip_name, "FH8856V201");
                break;
            case 0x00300001:
                strcpy(chip_name, "FH8858V201");
                break;
            }
            break;
        case 0x17092901:
            switch (ipver & 0xf) {
            case 0xc:
                strcpy(chip_name, "FH8852V100");
                break;
            case 0xd:
                strcpy(chip_name, "FH8856V100");
                break;
            }
            break;
        case 0x18112301:
            strcpy(chip_name, "FH8626V100");
            break;
        case 0x46488302:
            switch (ipver & 0x3f) {
            case 0x37:
                strcpy(chip_name, "FH8632");
                break;
            case 0x7:
                strcpy(chip_name, "FH8632v2");
                break;
            }
            break;
        case 0x20031601:
            switch (ipver & 0xc) {
            case 0xc:
                strcpy(chip_name, "FH8652");
                break;
            case 0x8:
                strcpy(chip_name, "FH8656");
                break;
            case 0x4:
                strcpy(chip_name, "FH8658");
                break;
            }
            break;
        default:
            strcpy(chip_name, buf);
            break;
        }
        strcpy(chip_manufacturer, VENDOR_FH);
        return true;
    }
}

static unsigned long fh_media_mem() {
    char buf[256];

    if (!get_regex_line_from_file("/proc/driver/vmm", "total.size=([0-9A-Fx]+)",
                                  buf, sizeof(buf)))
        return 0;
    return strtoul(buf, NULL, 10) * 1024;
}

unsigned long fh_totalmem(unsigned long *media_mem) {
    *media_mem = fh_media_mem();
    return kernel_mem();
}

int fh_open_sensor_fd() { return universal_open_sensor_fd("/dev/i2c-0"); }

static void fh_hal_cleanup() {}

float fh_get_temp() {
    float ret = -237.0;
    char buf[16];
    if (get_regex_line_from_file("/sys/class/thermal/thermal_zone0/temp",
                                 "(.+)", buf, sizeof(buf))) {
        ret = strtof(buf, NULL);
    }
    return ret;
}

void setup_hal_fh() {
    open_i2c_sensor_fd = fh_open_sensor_fd;
    close_sensor_fd = universal_close_sensor_fd;
    i2c_change_addr = universal_sensor_i2c_change_addr;
    i2c_read_register = universal_sensor_read_register;
    i2c_write_register = universal_sensor_write_register;
    possible_i2c_addrs = fh_possible_i2c_addrs;
    hal_cleanup = fh_hal_cleanup;
    if (!access("/sys/class/thermal/thermal_zone0/temp", R_OK))
        hal_temperature = fh_get_temp;
}
