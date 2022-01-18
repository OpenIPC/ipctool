#include <stdio.h>
#include <stdlib.h>

#include "chipid.h"
#include "hal_common.h"
#include "i2c.h"

#define SELECT_WIDE(reg_addr) reg_addr > 0xff ? 2 : 1

static int prepare_i2c_sensor(unsigned char i2c_addr) {
    if (!getchipid()) {
        puts("Unknown chip");
        exit(EXIT_FAILURE);
    }

    if (!open_i2c_sensor_fd) {
        puts("There is no platform specific I2C access layer");
        exit(EXIT_FAILURE);
    }

    int fd = open_i2c_sensor_fd();
    if (fd == -1) {
        puts("Device not found");
        exit(EXIT_FAILURE);
    }

    i2c_change_addr(fd, i2c_addr);

    return fd;
}

static int prepare_spi_sensor() {
    if (!getchipid()) {
        puts("Unknown chip");
        exit(EXIT_FAILURE);
    }

    if (!open_spi_sensor_fd) {
        puts("There is no platform specific SPI access layer");
        exit(EXIT_FAILURE);
    }

    int fd = open_spi_sensor_fd();
    if (fd == -1) {
        puts("Device not found");
        exit(EXIT_FAILURE);
    }

    return fd;
}

int i2cset(int argc, char **argv) {
    if (argc != 4) {
        puts("Usage: ipctool i2cset <device address> <register> <new value>");
        return EXIT_FAILURE;
    }

    unsigned char i2c_addr = strtoul(argv[1], 0, 16);
    unsigned int reg_addr = strtoul(argv[2], 0, 16);
    unsigned int reg_data = strtoul(argv[3], 0, 16);

    int fd = prepare_i2c_sensor(i2c_addr);

    int res = i2c_write_register(fd, i2c_addr, reg_addr, SELECT_WIDE(reg_addr),
                                 reg_data, 1);

    close_sensor_fd(fd);
    hal_cleanup();
    return EXIT_SUCCESS;
}

int i2cget(int argc, char **argv) {
    if (argc != 3) {
        puts("Usage: ipctool i2cget <device address> <register>");
        return EXIT_FAILURE;
    }

    unsigned char i2c_addr = strtoul(argv[1], 0, 16);
    unsigned int reg_addr = strtoul(argv[2], 0, 16);

    int fd = prepare_i2c_sensor(i2c_addr);

    int res =
        i2c_read_register(fd, i2c_addr, reg_addr, SELECT_WIDE(reg_addr), 1);
    printf("%#x\n", res);

    close_sensor_fd(fd);
    hal_cleanup();
    return EXIT_SUCCESS;
}

static void hexdump(read_register_t cb, int fd, unsigned char i2c_addr,
                        unsigned int from_reg_addr, unsigned int to_reg_addr) {
    char ascii[17] = {0};

    int size = to_reg_addr - from_reg_addr;
    printf("       0  1  2  3  4  5  6  7   8  9  A  B  C  D  E  F\n");
    for (size_t i = from_reg_addr; i < to_reg_addr; ++i) {
        int res = cb(fd, i2c_addr, i, SELECT_WIDE(i), 1);
        if (i == from_reg_addr)
            printf("%4.x: ", i);
        printf("%02X ", res);
        if (res >= ' ' && res <= '~') {
            ascii[i % 16] = res;
        } else {
            ascii[i % 16] = '.';
        }
        if ((i + 1) % 8 == 0 || i + 1 == size) {
            printf(" ");
            if ((i + 1) % 16 == 0) {
                printf("|  %s \n%4.x: ", ascii, i + 1);
            } else if (i + 1 == size) {
                ascii[(i + 1) % 16] = '\0';
                if ((i + 1) % 16 <= 8) {
                    printf(" ");
                }
                for (size_t j = (i + 1) % 16; j < 16; ++j) {
                    printf("   ");
                }
                printf("|  %s \n", ascii);
            }
        }
    }
    printf("\n");
}

int i2cdump(int argc, char **argv, bool script_mode) {
    if (argc != 4) {
        puts("Usage: ipctool [--script] i2cdump <device address> <from "
             "register> <to "
             "register>");
        return EXIT_FAILURE;
    }

    unsigned char i2c_addr = strtoul(argv[1], 0, 16);
    unsigned int from_reg_addr = strtoul(argv[2], 0, 16);
    unsigned int to_reg_addr = strtoul(argv[3], 0, 16);

    int fd = prepare_i2c_sensor(i2c_addr);

    if (script_mode) {
        for (size_t i = from_reg_addr; i < to_reg_addr; ++i)
            printf("ipctool i2cset %#x %#x %#x\n", i2c_addr, i,
                   i2c_read_register(fd, i2c_addr, i, SELECT_WIDE(i), 1));
    } else {
        hexdump(i2c_read_register, fd, i2c_addr, from_reg_addr, to_reg_addr);
    }

    close_sensor_fd(fd);
    hal_cleanup();

    return EXIT_SUCCESS;
}

int spidump(int argc, char **argv, bool script_mode) {
    if (argc != 3) {
        puts("Usage: ipctool [--script] spidump <from register> <to "
             "register>");
        return EXIT_FAILURE;
    }

    unsigned int from_reg_addr = strtoul(argv[1], 0, 16);
    unsigned int to_reg_addr = strtoul(argv[2], 0, 16);

    int fd = prepare_spi_sensor();

    if (script_mode) {
        for (size_t i = from_reg_addr; i < to_reg_addr; ++i)
            printf("ipctool spiset %#x %#x\n", i,
                   spi_read_register(fd, 0, i, SELECT_WIDE(i), 1));
    } else {
        hexdump(spi_read_register, fd, 0, from_reg_addr, to_reg_addr);
    }

    close_sensor_fd(fd);
    hal_cleanup();

    return EXIT_SUCCESS;
}
