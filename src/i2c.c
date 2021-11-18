#include <stdio.h>
#include <stdlib.h>

#include "chipid.h"
#include "hal_common.h"
#include "i2c.h"

void i2cget(char *arg, char *argv[]) {
    while (*argv != arg) {
        if (!*argv)
            break;

        argv++;
    }

    const char *addr = argv[0];
    const char *reg = argv[1];
    if (!reg) {
        puts("Usage: ipctool i2cget addr reg");
        return;
    }

    if (!getchipid()) {
        puts("Unkown chip");
        return;
    }

    if (!open_sensor_fd) {
        puts("There is no platform specific I2C/SPI access layer");
        return;
    }

    int fd = open_sensor_fd();
    if (fd == -1) {
        puts("Device not found");
        return;
    }

    unsigned char i2c_addr = strtoul(addr, 0, 16);
    unsigned int reg_addr = strtoul(reg, 0, 16);
    int res = sensor_read_register(fd, i2c_addr, reg_addr,
                                   reg_addr > 0xff ? 2 : 1, 1);
    printf("%#x\n", res);

    close_sensor_fd(fd);
    hal_cleanup();
}

void i2cdump(char *arg, char *argv[]) {
    while (*argv != arg) {
        if (!*argv)
            break;

        argv++;
    }

    const char *addr = argv[0];
    if (!argv[1] || !argv[2]) {
        puts("Usage: ipctool i2cdump addr from_reg to_reg");
        return;
    }
    const char *from_reg = argv[1];
    const char *to_reg = argv[2];

    if (!getchipid()) {
        puts("Unkown chip");
        return;
    }

    if (!open_sensor_fd) {
        puts("There is no platform specific I2C/SPI access layer");
        return;
    }

    int fd = open_sensor_fd();
    if (fd == -1) {
        puts("Device not found");
        return;
    }

    unsigned char i2c_addr = strtoul(addr, 0, 16);
    unsigned int from_reg_addr = strtoul(from_reg, 0, 16);
    unsigned int to_reg_addr = strtoul(to_reg, 0, 16);

    for (int i = from_reg_addr; i < to_reg_addr; i++) {
        int res = sensor_read_register(fd, i2c_addr, i, i > 0xff ? 2 : 1, 1);
        printf("%#x %#x\n", i, res);
    }

    close_sensor_fd(fd);
    hal_cleanup();
}
