#include <string.h>
#include <stdbool.h>

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "chipid.h"
#include "hal_common.h"

int (*open_sensor_fd)();
int (*sensor_read_register)(int fd, unsigned char i2c_addr,
                            unsigned int reg_addr, unsigned int reg_width,
                            unsigned int data_width);
int (*sensor_write_register)(int fd, unsigned char i2c_addr,
                             unsigned int reg_addr, unsigned int reg_width,
                             unsigned int data, unsigned int data_width);

int common_open_sensor_fd(const char* dev_name) {
    int fd;

    fd = open(dev_name, O_RDWR);
    if (fd < 0) {
        printf("Open %s error!\n", dev_name);
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

void setup_hal_drivers() {
    if (!strcmp(VENDOR_HISI, chip_manufacturer))
        setup_hal_hisi();
    else if (!strcmp(VENDOR_XM, chip_manufacturer))
        setup_hal_xm();
}
