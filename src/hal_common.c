#include <stdbool.h>
#include <string.h>

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "chipid.h"
#include "hal_common.h"

sensor_addr_t* possible_i2c_addrs;

int (*open_sensor_fd)();
int (*sensor_read_register)(int fd, unsigned char i2c_addr,
                            unsigned int reg_addr, unsigned int reg_width,
                            unsigned int data_width);
int (*sensor_write_register)(int fd, unsigned char i2c_addr,
                             unsigned int reg_addr, unsigned int reg_width,
                             unsigned int data, unsigned int data_width);
int (*sensor_i2c_change_addr)(int fd, unsigned char addr);

int common_open_sensor_fd(const char *dev_name) {
    int fd;

    fd = open(dev_name, O_RDWR);
    if (fd < 0) {
#ifndef NDEBUG
        printf("Open %s error!\n", dev_name);
#endif
        return -1;
    }

    return fd;
}

bool common_close_sensor_fd(int fd) {
    if (fd >= 0) {
        close(fd);
        return true;
    }
    return false;
}

// Set I2C slave address,
// actually do nothing
int common_sensor_i2c_change_addr(int fd, unsigned char addr) { return 0; }

void setup_hal_drivers() {
    if (!strcmp(VENDOR_HISI, chip_manufacturer))
        setup_hal_hisi();
    else if (!strcmp(VENDOR_XM, chip_manufacturer))
        setup_hal_xm();
}
