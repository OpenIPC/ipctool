#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/ioctl.h>
#include <unistd.h>

#include "hal/common.h"
#include "ram.h"
#include "tools.h"

static unsigned char sony_addrs[] = {0x1a, 0};
static unsigned char onsemi_addrs[] = {0x10, 0};
static unsigned char soi_addrs[] = {0x30, 0x40, 0};
static unsigned char ssens_addrs[] = {0x30, 0};
static unsigned char omni_addrs[] = {0x36, 0};
// only for reference, used in XM510
static unsigned char bg_addrs[] = {0x32, 0};

sensor_addr_t xm_possible_i2c_addrs[] = {{SENSOR_SONY, sony_addrs},
                                         {SENSOR_SMARTSENS, ssens_addrs},
                                         {SENSOR_ONSEMI, onsemi_addrs},
                                         {SENSOR_SOI, soi_addrs},
                                         {SENSOR_OMNIVISION, omni_addrs},
                                         {SENSOR_BRIGATES, bg_addrs},
                                         {0, NULL}};

int xm_open_sensor_fd() { return universal_open_sensor_fd("/dev/xm_i2c"); }

int xm_sensor_read_register(int fd, unsigned char i2c_addr,
                            unsigned int reg_addr, unsigned int reg_width,
                            unsigned int data_width) {
    int ret;
    I2C_DATA_S i2c_data;

    i2c_data.dev_addr = i2c_addr;
    i2c_data.reg_addr = reg_addr;
    i2c_data.addr_byte_num = reg_width;
    i2c_data.data_byte_num = data_width;

    ret = ioctl(fd, CMD_I2C_READ, &i2c_data);
    if (ret) {
        printf("xm_i2c read failed!\n");
        return -1;
    }

    return i2c_data.data;
}

int xm_sensor_write_register(int fd, unsigned char i2c_addr,
                             unsigned int reg_addr, unsigned int reg_width,
                             unsigned int data, unsigned int data_width) {
    int ret;
    I2C_DATA_S i2c_data;

    i2c_data.dev_addr = i2c_addr;
    i2c_data.reg_addr = reg_addr;
    i2c_data.addr_byte_num = reg_width;
    i2c_data.data = data;
    i2c_data.data_byte_num = data_width;

    ret = ioctl(fd, CMD_I2C_WRITE, &i2c_data);

    if (ret) {
        printf("xm_i2c write failed!\n");
        return -1;
    }

    return 0;
}

static void xm_hal_cleanup() { restore_printk(); }

void setup_hal_xm() {
    disable_printk();
    open_i2c_sensor_fd = xm_open_sensor_fd;
    i2c_change_addr = dummy_sensor_i2c_change_addr;
    i2c_read_register = xm_sensor_read_register;
    i2c_write_register = xm_sensor_write_register;
    possible_i2c_addrs = xm_possible_i2c_addrs;
    hal_cleanup = xm_hal_cleanup;
#ifndef STANDALONE_LIBRARY
    hal_totalmem = xm_totalmem;
#endif
}

static unsigned long xm_media_mem() {
    FILE *f = fopen("/proc/umap/mmz", "r");
    if (!f)
        return 0;

    unsigned long mmem = 0;
    regex_t regex;
    regmatch_t matches[3];
    if (!regex_compile(&regex, "\\[(0x[0-9a-f]+)-+(0x[0-9a-f]+)\\]"))
        goto exit;

    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    while ((read = getline(&line, &len, f)) != -1) {
        if (regexec(&regex, line, sizeof(matches) / sizeof(matches[0]),
                    (regmatch_t *)&matches, 0) == 0) {
            regoff_t start = matches[1].rm_so;
            regoff_t end = matches[1].rm_eo;
            line[end] = 0;
            unsigned long memstart = strtoul(line + start, NULL, 16);

            start = matches[2].rm_so;
            end = matches[2].rm_eo;
            line[end] = 0;
            unsigned long memend = strtoul(line + start, NULL, 16);

            mmem = (memend - memstart) / 1024;
            break;
        }
    }
    if (line)
        free(line);

exit:
    regfree(&regex);
    fclose(f);
    return mmem;
}

unsigned long xm_totalmem(unsigned long *media_mem) {
    *media_mem = xm_media_mem();
    return *media_mem + kernel_mem();
}

bool xm_detect_cpu(char *chip_name, uint32_t base) {
    char buf[256];
    (void)base;

    bool res =
        line_from_file("/proc/cpuinfo", "^Hardware.+(xm.+)", buf, sizeof(buf));
    if (!res) {
        return false;
    }
    strcpy(chip_name, buf);
    char *ptr = chip_name;
    while (*ptr) {
        *ptr = toupper(*ptr);
        ptr++;
    }
    if (!strcmp(chip_name, "XM530")) {
        uint32_t reg;
        if (mem_reg(0x100B001C, (uint32_t *)&reg, OP_READ)) {
            // bank_size, 0x2 for 512M and 0x3 for 1024M
            if (reg == 3)
                strcpy(chip_name, "XM550");
        }
    }

    return true;
}
