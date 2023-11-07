#include "hal/ingenic.h"

#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include "chipid.h"
#include "hal/common.h"
#include "tools.h"

static unsigned char sony_addrs[] = {0x34, 0};
static unsigned char ssens_addrs[] = {0x60, 0};
static unsigned char omni_addrs[] = {0x6c, 0};
static unsigned char onsemi_addrs[] = {0x20, 0};
static unsigned char gc_addrs[] = {0x6e, 0x52, 0x42, 0};
static unsigned char soi_addrs[] = {0x60, 0x80, 0};

static sensor_addr_t ingenic_possible_i2c_addrs[] = {
    {SENSOR_SONY, sony_addrs},
    {SENSOR_SMARTSENS, ssens_addrs},
    {SENSOR_ONSEMI, onsemi_addrs},
    {SENSOR_OMNIVISION, omni_addrs},
    {SENSOR_GALAXYCORE, gc_addrs},
    {SENSOR_SOI, soi_addrs},
    {0, NULL}};

typedef unsigned char uint8;
typedef unsigned short uint16;

#define _BYTE uint8
#define _WORD uint16

#define HIWORD(x) (*((_WORD *)&(x) + 1))
#define BYTEn(x, n) (*((_BYTE *)&(x) + n))
#define BYTE2(x) BYTEn(x, 2)

static int get_cpu_id() {
    uint32_t soc_id = 0, cppsr = 0;
    uint32_t subsoctype = 0, subremark = 0, subsoctypet40 = 0, subsoctypet41 = 0;

    if (!mem_reg(0x1300002C, &soc_id, OP_READ))
        return -1;
    if (!mem_reg(0x10000034, &cppsr, OP_READ))
        return -1;
    if (!mem_reg(0x13540238, &subsoctype, OP_READ))
        return -1;
    if (!mem_reg(0x13540231, &subremark, OP_READ))
        return -1;
    if (soc_id >> 28 != 1)
        return -1;
    switch ((soc_id << 4) >>
            0x10) { // T10/T20 have different calculation method
    case 5:
        chip_generation = 0x10;
        switch ((uint8_t)cppsr) {
        case 0:
            return 1;
        case 1:
            return 0;
        case 0x10:
            return 2;
        default:
            return -1;
        }
    case 0x2000:
        chip_generation = 0x20;
        switch ((uint8_t)cppsr) {
        case 1:
            return 3;
        case 16:
            return 4;
        default:
            return -1;
        }
    }
    switch ((soc_id >> 12) & 0xff) {
    case 0x30:
        chip_generation = 0x30;
        if ((uint8_t)cppsr == 1) {
            switch (HIWORD(subsoctype)) {
            case 0x1111:
                return 7;
            case 0x3333:
                return 7;
            case 0x2222:
                return 8;
            case 0x4444:
                return 9;
            case 0x5555:
                return 10;
            default:
                return 8;
            }
        } else {
            if ((uint8_t)cppsr == 0x10)
                return 6;
            return -1;
        }
    case 0x21:
        chip_generation = 0x21;
        if ((uint8_t)cppsr == 1) {
            if (HIWORD(subsoctype) != 0x3333) {
                if (HIWORD(subsoctype) != 0x1111) {
                    if (HIWORD(subsoctype) == 0x5555)
                        return 14;
                    return 13;
                }
                return 12;
            }
            return 11;
        } else if ((uint8_t)cppsr != 0x10) {
            return -1;
        }
        return 11;
    case 0x31:
        chip_generation = 0x31;
        if ((uint8_t)cppsr == 1) {
            if (BYTE2(subremark)) {
                if (BYTE2(subremark) != (uint8_t)cppsr) {
                    if (BYTE2(subremark) != 3) {
                        if (BYTE2(subremark) != 7) {
                            if (BYTE2(subremark) != 0xF)
                                return -1;
                            return 15;
                        }
                        return 16;
                    }
                    return 15;
                }
                return 16;
            } else {
                switch (HIWORD(subsoctype)) {
                case 0x1111:
                    return 16;
                case 0x2222:
                    return 17;
                case 0x3333:
                    return 15;
                case 0x4444:
                    return 18;
                case 0x5555:
                    return 20;
                case 0x6666:
                    return 23;
                case 0xcccc:
                    return 19;
                case 0xdddd:
                    return 21;
                case 0xeeee:
                    return 22;
                default:
                    return 16;
                }
            }
        } else if ((uint8_t)cppsr != 0x10) {
            return -1;
        }
        return 15;
    case 0x40:
        chip_generation = 0x40;
        if (!mem_reg(0x13540250, &subsoctypet40, OP_READ))
            return -1;
        switch (HIWORD(subsoctypet40)) {
        case 0x1111:
            return 24;
        case 0x7777:
            return 25;
        case 0x4444:
            return 26;
        default:
            return -1;
        }
    case 0x41:
        chip_generation = 0x41;
        if (!mem_reg(0x13540250, &subsoctypet41, OP_READ))
            return -1;
        switch (HIWORD(subsoctypet41)) {
        case 0x33331111:
            return 28;
        case 0x55551111:
            return 29;
        case 0x88881111:
            return 30;
        case 0x99991111:
            return 31;
        case 0x11112222:
            return 32;
        case 0x77772222:
            return 33;
        case 0xAAAA2222:
            return 34;
        case 0x66662222:
            return 35;
        default:
            return -1;
        }
    default:
        return -1;
    }
}

static const char *ingenic_cpu_name() {
    switch (get_cpu_id()) {
    case 0:
        return "T10";
    case 1:
    case 2:
        return "T10L";
    case 3:
        return "T20";
    case 4:
        return "T20L";
    case 5:
        return "T20X";
    case 6:
        return "T30L";
    case 7:
        return "T30N";
    case 8:
        return "T30X";
    case 9:
        return "T30A";
    case 10:
        return "T30Z";
    case 11:
        return "T21L";
    case 12:
        return "T21N";
    case 13:
        return "T21X";
    case 14:
        return "T21Z";
    case 15:
        return "T31L";
    case 16:
        return "T31N";
    case 17:
        return "T31X";
    case 18:
        return "T31A";
    case 19:
        return "T31AL";
    case 20:
        return "T31ZL";
    case 21:
        return "T31ZC";
    case 22:
        return "T31LC";
    case 23:
        return "T31ZX";
    case 24:
        return "T40N";
    case 25:
        return "T40XP";
    case 26:
        return "T40A";
    case 27:
        return "T41A";
    case 28:
        return "T41L";
    case 29:
        return "T41ZL";
    case 30:
        return "T41LC";
    case 31:
        return "T41LQ";
    case 32:
        return "T41N";
    case 33:
        return "T41ZN";
    case 34:
        return "T41NQ";
    case 35:
        return "T41X";
    }
    return "unknown";
}

bool ingenic_detect_cpu(char *chip_name) {

    // chipid (serial number?)
    // 0x13540200: 0xEBD9102F
    // 0x13540204: 0x3A922801
    // 0x13540208: 0x00001456
    // result: 2f10d9eb0128923a56140000

    strcpy(chip_name, ingenic_cpu_name());
    return true;
}

static unsigned long ingenic_media_mem() {
    char buf[256];

    if (!line_from_file("/proc/cmdline", "rmem=([0-9x]+)", buf, sizeof(buf)))
        return 0;
    return strtoul(buf, NULL, 10);
}

unsigned long ingenic_totalmem(unsigned long *media_mem) {
    *media_mem = (ingenic_media_mem() < 1024) ? ingenic_media_mem() * 1024
                                              : ingenic_media_mem();
    return kernel_mem();
}

float ingenic_get_temp() {
    float ret = -237.0;
    char buf[16];
    if (line_from_file("/sys/class/thermal/thermal_zone0/temp", "(.+)", buf,
                       sizeof(buf))) {
        ret = strtof(buf, NULL);
    }
    return ret;
}

static int ingenic_open_i2c_fd() {
    int adapter_nr = 0;
    if (!strncmp(chip_name, "T40", 3))
        adapter_nr = 1;
    char adapter_name[FILENAME_MAX];

    snprintf(adapter_name, sizeof(adapter_name), "/dev/i2c-%d", adapter_nr);

    return universal_open_sensor_fd(adapter_name);
}

static void ingenic_enable_sensor_clock() {
    FILE *f;
    char buf[16];

    if (line_from_file("/proc/jz/clock/cgu_cim/enable", "(.+)", buf,
                       sizeof(buf))) {
        if (!strcmp(buf, "disabled")) {
            f = fopen("/proc/jz/clock/cgu_cim/enable", "w");
            fprintf(f, "%s", "1");
            uint32_t direct;
            mem_reg(0x10010030, &direct, OP_READ);
            direct &= ~(1 << 18);
            mem_reg(0x10010030, &direct, OP_WRITE);
            mem_reg(0x10010040, &direct, OP_READ);
            direct |= 1 << 18;
            mem_reg(0x10010040, &direct, OP_WRITE);
            fclose(f);
        }
    }
}

void setup_hal_ingenic() {
    disable_printk();
    ingenic_enable_sensor_clock();
    possible_i2c_addrs = ingenic_possible_i2c_addrs;
    open_i2c_sensor_fd = ingenic_open_i2c_fd;
#ifndef STANDALONE_LIBRARY
    hal_totalmem = ingenic_totalmem;
#endif
}
