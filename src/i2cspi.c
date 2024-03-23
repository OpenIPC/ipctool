#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chipid.h"
#include "hal/common.h"
#include "i2cspi.h"

#define SELECT_WIDE(reg_addr) reg_addr > 0xff ? 2 : 1

int i2c_adapter_nr = 0;

static int prepare_i2c_sensor(unsigned char i2c_addr) {
    if (!getchipname()) {
        puts("Unknown chip");
        exit(EXIT_FAILURE);
    }

    int fd = open_i2c_sensor_fd(i2c_adapter_nr);
    if (fd == -1) {
        puts("Device not found");
        exit(EXIT_FAILURE);
    }

    i2c_change_addr(fd, i2c_addr);

    return fd;
}

static int prepare_spi_sensor() {
    if (!getchipname()) {
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

static int i2cset(int argc, char **argv) {
    if (argc != 3) {
        puts("Usage: ipctool i2cset <device address> <register> <new value>");
        return EXIT_FAILURE;
    }

    unsigned char i2c_addr = strtoul(argv[0], 0, 16);
    unsigned int reg_addr = strtoul(argv[1], 0, 16);
    unsigned int reg_data = strtoul(argv[2], 0, 16);

    int fd = prepare_i2c_sensor(i2c_addr);

    int res = i2c_write_register(fd, i2c_addr, reg_addr, SELECT_WIDE(reg_addr),
                                 reg_data, 1);

    close_sensor_fd(fd);
    hal_cleanup();
    return EXIT_SUCCESS;
}

static int spiset(int argc, char **argv) {
    if (argc != 2) {
        puts("Usage: ipctool spiset <register> <new value>");
        return EXIT_FAILURE;
    }

    unsigned int reg_addr = strtoul(argv[0], 0, 16);
    unsigned int reg_data = strtoul(argv[1], 0, 16);

    int fd = prepare_spi_sensor();

    int res =
        spi_write_register(fd, 0, reg_addr, SELECT_WIDE(reg_addr), reg_data, 1);

    close_sensor_fd(fd);
    hal_cleanup();
    return EXIT_SUCCESS;
}

static int i2cget(int argc, char **argv) {
    if (argc != 2) {
        puts("Usage: ipctool i2cget <device address> <register>");
        return EXIT_FAILURE;
    }

    unsigned char i2c_addr = strtoul(argv[0], 0, 16);
    unsigned int reg_addr = strtoul(argv[1], 0, 16);

    int fd = prepare_i2c_sensor(i2c_addr);

    int res =
        i2c_read_register(fd, i2c_addr, reg_addr, SELECT_WIDE(reg_addr), 1);
    printf("%#x\n", res);

    close_sensor_fd(fd);
    hal_cleanup();
    return EXIT_SUCCESS;
}

static int spiget(int argc, char **argv) {
    if (argc != 1) {
        puts("Usage: ipctool spiget <register>");
        return EXIT_FAILURE;
    }

    unsigned int reg_addr = strtoul(argv[0], 0, 16);

    int fd = prepare_spi_sensor();

    int res = spi_read_register(fd, 0, reg_addr, SELECT_WIDE(reg_addr), 1);
    printf("%#x\n", res);

    close_sensor_fd(fd);
    hal_cleanup();
    return EXIT_SUCCESS;
}

static void hexdump(read_register_t cb, int fd, unsigned char i2c_addr,
                    unsigned int from_reg_addr, unsigned int to_reg_addr) {
    char ascii[17] = {0};

    size_t size = to_reg_addr - from_reg_addr;
    printf("       0  1  2  3  4  5  6  7   8  9  A  B  C  D  E  F\n");
    for (size_t i = from_reg_addr; i <= to_reg_addr; ++i) {
        int res = cb(fd, i2c_addr, i, SELECT_WIDE(i), 1);
        if (i % 16 == 0)
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
                printf("|  %s \n", ascii);
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

static int i2cdump(int argc, char **argv, bool script_mode) {
    if (argc != 3) {
        puts("Usage: ipctool [--script] i2cdump <device address> <from "
             "register> <to "
             "register>");
        return EXIT_FAILURE;
    }

    unsigned char i2c_addr = strtoul(argv[0], 0, 16);
    unsigned int from_reg_addr = strtoul(argv[1], 0, 16);
    unsigned int to_reg_addr = strtoul(argv[2], 0, 16);

    int fd = prepare_i2c_sensor(i2c_addr);

    if (script_mode) {
        for (size_t i = from_reg_addr; i <= to_reg_addr; ++i)
            printf("ipctool i2cset %#x %#x %#x\n", i2c_addr, i,
                   i2c_read_register(fd, i2c_addr, i, SELECT_WIDE(i), 1));
    } else {
        hexdump(i2c_read_register, fd, i2c_addr, from_reg_addr, to_reg_addr);
    }

    close_sensor_fd(fd);
    hal_cleanup();

    return EXIT_SUCCESS;
}

static int i2cdetect(int argc, char **argv, bool script_mode) {
    (void)argv;
    (void)script_mode;

    if (argc != 0) {
        puts("Usage: ipctool i2cdetect");
        return EXIT_FAILURE;
    }

    unsigned char i2c_addr;

    printf("       0  1  2  3  4  5  6  7   8  9  a  b  c  d  e  f\n");
    i2c_addr = 0xff;  // will be 0x00 after first increment
    do {
        ++i2c_addr;
        int fd = prepare_i2c_sensor(i2c_addr);
        int res = i2c_read_register(fd, i2c_addr, 0, SELECT_WIDE(0), 1);

        if (i2c_addr % 16 == 0)
            printf("%4.x: ", i2c_addr);

        if (res != -1) {
            printf("%02x ", i2c_addr);
        } else {
            printf("xx ");
        }

        if ((i2c_addr + 1) % 8 == 0 ) {
            printf(" ");
            if ((i2c_addr + 1) % 16 == 0)
                printf("|  \n");
        }

        close_sensor_fd(fd);
        hal_cleanup();
    } while (i2c_addr != 0xff);
    
    printf("\n");

    return EXIT_SUCCESS;
}

static int spidump(int argc, char **argv, bool script_mode) {
    if (argc != 2) {
        puts("Usage: ipctool [--script] spidump <from register> <to "
             "register>");
        return EXIT_FAILURE;
    }

    unsigned int from_reg_addr = strtoul(argv[0], 0, 16);
    unsigned int to_reg_addr = strtoul(argv[1], 0, 16);

    int fd = prepare_spi_sensor();

    if (script_mode) {
        for (size_t i = from_reg_addr; i <= to_reg_addr; ++i)
            printf("ipctool spiset %#x %#x\n", i,
                   spi_read_register(fd, 0, i, SELECT_WIDE(i), 1));
    } else {
        hexdump(spi_read_register, fd, 0, from_reg_addr, to_reg_addr);
    }

    close_sensor_fd(fd);
    hal_cleanup();

    return EXIT_SUCCESS;
}

extern void print_usage();

int i2cspi_cmd(int argc, char **argv) {
    const char *short_options = "sb:";
    const struct option long_options[] = {
        {"script", no_argument, NULL, 's'},
        {"bus", 1, NULL, 'b'},
        {NULL, 0, NULL, 0},
    };
    bool script_mode = false;
    int res;
    int option_index;
    int flags;

    while ((res = getopt_long_only(argc, argv, short_options, long_options,
                                   &option_index)) != -1) {
        switch (res) {
        case 's':
            script_mode = true;
            break;
        case 'b':
            flags = strtoul(optarg, NULL, 0);
            i2c_adapter_nr = flags;
            break;
        case '?':
            print_usage();
            return EXIT_FAILURE;
        }
    }

    bool i2c_mode = argv[0][0] == 'i';
    if (!strcmp(argv[0] + 3, "get")) {
        if (i2c_mode)
            return i2cget(argc - optind, argv + optind);
        else
            return spiget(argc - optind, argv + optind);
    } else if (!strcmp(argv[0] + 3, "set")) {
        if (i2c_mode)
            return i2cset(argc - optind, argv + optind);
        else
            return spiset(argc - optind, argv + optind);
    } else if (!strcmp(argv[0] + 3, "dump")) {
        if (i2c_mode)
            return i2cdump(argc - optind, argv + optind, script_mode);
        else
            return spidump(argc - optind, argv + optind, script_mode);
    } else if (!strcmp(argv[0] + 3, "detect")) {
        if (i2c_mode)
            return i2cdetect(argc - optind, argv + optind, script_mode);
    }

    print_usage();
    return EXIT_FAILURE;
}
