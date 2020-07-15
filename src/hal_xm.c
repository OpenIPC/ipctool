#include <stdio.h>
#include <string.h>

#include <sys/ioctl.h>

#include "hal_common.h"

static unsigned char onsemi_addrs[] = {0x10, 0};
static unsigned char soi_addrs[] = {0x30, 0};
static unsigned char ssens_addrs[] = {0x30, 0};

sensor_addr_t xm_possible_i2c_addrs[] = {{SENSOR_ONSEMI, onsemi_addrs},
                                         {SENSOR_SOI, soi_addrs},
                                         {SENSOR_SMARTSENS, ssens_addrs},
                                         {0, NULL}};

int xm_open_sensor_fd() {
    int adapter_nr = 0; /* probably dynamically determined */
    char adap_name[FILENAME_MAX] = {0};
    strcpy(adap_name, "/dev/xm_i2c");

    if (adapter_nr) {
        sprintf(adap_name + strlen(adap_name), "%d", adapter_nr);
    }

    return common_open_sensor_fd(adap_name);
}

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
    sensor_i2c_change_addr = common_sensor_i2c_change_addr;
    sensor_read_register = xm_sensor_read_register;
    sensor_write_register = xm_sensor_write_register;
    possible_i2c_addrs = xm_possible_i2c_addrs;
}
