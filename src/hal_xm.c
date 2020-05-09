#include <stdio.h>

#include <sys/ioctl.h>

#include "hal_common.h"

int xm_open_sensor_fd() { return common_open_sensor_fd("/dev/xm_i2c"); }

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

void setup_hal_xm() {
    open_sensor_fd = xm_open_sensor_fd;
    sensor_read_register = xm_sensor_read_register;
    sensor_write_register = xm_sensor_write_register;
}
