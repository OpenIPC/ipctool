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

    sensor_i2c_change_addr(fd, i2c_addr);

    int res = sensor_read_register(fd, i2c_addr, reg_addr,
                                   reg_addr > 0xff ? 2 : 1, 1);
    printf("%#x\n", res);

    close_sensor_fd(fd);
    hal_cleanup();
}

void i2cdump(char *arg, char *argv[]) {
    char ascii[17];
    size_t i, j;
    ascii[16] = '\0';

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


    sensor_i2c_change_addr(fd, i2c_addr);

    int size = to_reg_addr-from_reg_addr;
    for (i = from_reg_addr; i < to_reg_addr; ++i) {
        int res = sensor_read_register(fd, i2c_addr, i, i > 0xff ? 2 : 1, 1);
        if (i == from_reg_addr)
            printf("%x: ", i);
        printf("%02X ", res);
        if (res >= ' ' && res <= '~') {
            ascii[i % 16] = res;
        } else {
            ascii[i % 16] = '.';
        }
        if ((i + 1) % 8 == 0 || i + 1 == size) {
            printf(" ");
            if ((i + 1) % 16 == 0) {
                printf("|  %s \n%x: ", ascii, i+1);
            } else if (i + 1 == size) {
                ascii[(i + 1) % 16] = '\0';
                if ((i + 1) % 16 <= 8) {
                    printf(" ");
                }
                for (j = (i + 1) % 16; j < 16; ++j) {
                    printf("   ");
                }
                printf("|  %s \n", ascii);
            }
        }
    }

    close_sensor_fd(fd);
    hal_cleanup();
}
