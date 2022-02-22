#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "chipid.h"
#include "hal_common.h"
#include "snstool.h"
#include "tools.h"

extern void Help();

typedef struct {
    const char *name;
    uint16_t base_addr;
    uint8_t len;
} Reg;

const Reg sc2315e_regs[] = {
    {"EXP", 0x3e00, 3},
    {"AGAIN", 0x3e08, 2},
    {"DGAIN", 0x3e06, 2},
    {"VMAX", 0x320e, 2},
    {"R3301", 0x3301, 1},
    {"R3314", 0x3314, 1},
    {"R3632", 0x3632, 1},
    {"R3812", 0x3812, 1},
    {"R5781", 0x5781, 1},
    {"R5785", 0x5785, 1},
    {NULL},
};

const Reg imx385_regs[] = {
    {"SHS1", 0x3020, 3}, {"GAIN", 0x3014, 2},
    {"HCG", 0x3009, 1},  {"SHS2", 0x3018, 3},
    {"VMAX", 0x3018, 3}, {"RHS1", 0x302C, 3},
    {"YOUT", 0x3357, 2}, {NULL},
};

struct {
    const char *sns_name;
    const Reg *reg;
    uint8_t be;
} sns_regs[] = {
    {"SC2315E", sc2315e_regs, .be = 1},
    {"IMX385", imx385_regs},
};

static int prepare_i2c_sensor(unsigned char i2c_addr) {
    int fd = open_i2c_sensor_fd();
    if (fd == -1) {
        puts("Device not found");
        exit(EXIT_FAILURE);
    }

    i2c_change_addr(fd, i2c_addr);

    return fd;
}

static int prepare_spi_sensor() {
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

static read_register_t read_register;

static uint64_t readout_i2c_reg(int fd, sensor_ctx_t *ctx, const Reg *reg,
                                bool be) {
    uint64_t value = 0;
    for (int i = 0; i < reg->len; i++) {
        value <<= 8;
        unsigned int reg_addr = reg->base_addr;
        if (be)
            reg_addr += i;
        else
            reg_addr += reg->len - i - 1;

        value |= read_register(fd, ctx->addr, reg_addr, ctx->reg_width,
                               ctx->data_width);
    }

    return value;
}

static int monitor_sensor(sensor_ctx_t *ctx, const Reg *reg, bool be) {
    int fd = -1;

    if (!strcmp(ctx->control, "i2c")) {
        read_register = i2c_read_register;
        fd = prepare_i2c_sensor(ctx->addr);
    } else {
        read_register = spi_read_register;
        fd = prepare_spi_sensor();
    }

    while (1) {
        const Reg *ptr = reg;
        while (ptr->name) {
            printf("%s\t%llx\t", ptr->name, readout_i2c_reg(fd, ctx, ptr, be));
            ptr++;
        }

        printf("\n");
        sleep(2);
    }
    return EXIT_SUCCESS;
}

static int monitor() {
    sensor_ctx_t ctx;
    if (!getsensorid(&ctx)) {
        fprintf(stderr, "No sensor detected\n");
        return EXIT_FAILURE;
    }

    for (int i = 0; i < ARRAY_SIZE(sns_regs); i++) {
        if (!strcmp(sns_regs[i].sns_name, ctx.sensor_id))
            return monitor_sensor(&ctx, sns_regs[i].reg, sns_regs[i].be);
    }

    fprintf(stderr, "Sensor %s is not supported\n", ctx.sensor_id);
    return EXIT_FAILURE;
}

int snstool_cmd(int argc, char **argv) {
    if (argc != 2) {
        Help();
        return EXIT_FAILURE;
    }

    return monitor();
}
