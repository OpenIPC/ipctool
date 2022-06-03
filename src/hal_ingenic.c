#include "hal_ingenic.h"

#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include "chipid.h"
#include "hal_common.h"
#include "tools.h"

static unsigned char sony_addrs[] = {0x34, 0};
static unsigned char ssens_addrs[] = {0x60, 0};
static unsigned char omni_addrs[] = {0x6c, 0};
static unsigned char onsemi_addrs[] = {0x20, 0};
static unsigned char gc_addrs[] = {0x6e, 0};

sensor_addr_t ingenic_possible_i2c_addrs[] = {
    {SENSOR_SONY, sony_addrs},     {SENSOR_SMARTSENS, ssens_addrs},
    {SENSOR_ONSEMI, onsemi_addrs}, {SENSOR_OMNIVISION, omni_addrs},
    {SENSOR_GALAXYCORE, gc_addrs}, {0, NULL}};

typedef unsigned char uint8;
typedef unsigned short uint16;

#define _BYTE uint8
#define _WORD uint16

#define HIWORD(x) (*((_WORD *)&(x) + 1))
#define BYTEn(x, n) (*((_BYTE *)&(x) + n))
#define BYTE2(x) BYTEn(x, 2)

static int get_cpu_id() {
    int result; // $v0
    int v2;     // $v0
    int v3;     // $a0
    int Option; // $v0
    int v5;     // $a1
    int v6;     // $v1
    int v7;     // $a0
    int v8;     // $a0
    int v9;     // $a0
    int v10;    // $v0
    int v11;    // $v0

    uint32_t soc_id_7971 = 0, cppsr_7970 = 0;
    uint32_t subsoctype_7972 = 0, subremark_7974 = 0;

    if (!mem_reg(0x1300002C, &soc_id_7971, OP_READ))
        return -1;
    if (!mem_reg(0x10000034, &cppsr_7970, OP_READ))
        return -1;
    if (!mem_reg(0x13540238, &subsoctype_7972, OP_READ))
        return -1;
    if (!mem_reg(0x13540231, &subremark_7974, OP_READ))
        return -1;
    if (soc_id_7971 >> 28 != 1)
        return -1;
    switch ((soc_id_7971 >> 12) & 0xff) {
    case 5:
        if ((uint8_t)cppsr_7970 == 1) {
            return 0;
        } else {
            result = 1;
            if ((_BYTE)cppsr_7970) {
                result = 2;
                if ((uint8_t)cppsr_7970 != 0x10)
                    return -1;
            }
        }
        break;
    case 0x2000:
        if ((uint8_t)cppsr_7970 == 1)
            return 3;
        if ((uint8_t)cppsr_7970 == 16)
            return 4;
        return -1;
    case 0x30:
        if ((uint8_t)cppsr_7970 == 1) {
            if (HIWORD(subsoctype_7972) == 0x3333) {
                return 7;
            } else {
                result = 7;
                if (HIWORD(subsoctype_7972) != 0x1111) {
                    result = 8;
                    if (HIWORD(subsoctype_7972) != 0x2222) {
                        result = 9;
                        if (HIWORD(subsoctype_7972) != 0x4444) {
                            result = 8;
                            if (HIWORD(subsoctype_7972) == 21845)
                                return 10;
                        }
                    }
                }
            }
        } else {
            v6 = 6;
            if ((uint8_t)cppsr_7970 != 0x10)
                return -1;
            return v6;
        }
        break;
    case 0x21:
        result = 11;
        if ((uint8_t)cppsr_7970 == 1) {
            if (BYTE2(subremark_7974)) {
                result = 12;
                if (BYTE2(subremark_7974) != (uint8_t)cppsr_7970) {
                    result = 11;
                    if (BYTE2(subremark_7974) != 3) {
                        result = 12;
                        if (BYTE2(subremark_7974) != 7) {
                            v8 = 11;
                            if (BYTE2(subremark_7974) != 0xF)
                                return -1;
                            return v8;
                        }
                    }
                }
            } else {
                result = 11;
                if (HIWORD(subsoctype_7972) != 0x3333) {
                    result = 12;
                    if (HIWORD(subsoctype_7972) != 0x1111) {
                        v9 = 13;
                        if (HIWORD(subsoctype_7972) == 21845)
                            return 14;
                        return v9;
                    }
                }
            }
        } else if ((uint8_t)cppsr_7970 != 0x10) {
            return -1;
        }
        break;
    case 0x31:
        result = 15;
        if ((uint8_t)cppsr_7970 == 1) {
            if (BYTE2(subremark_7974)) {
                result = 16;
                if (BYTE2(subremark_7974) != (uint8_t)cppsr_7970) {
                    result = 15;
                    if (BYTE2(subremark_7974) != 3) {
                        result = 16;
                        if (BYTE2(subremark_7974) != 7) {
                            v7 = 15;
                            if (BYTE2(subremark_7974) != 0xF)
                                return -1;
                            return v7;
                        }
                    }
                }
            } else {
                result = 15;
                if (HIWORD(subsoctype_7972) != 0x3333) {
                    result = 16;
                    if (HIWORD(subsoctype_7972) != 0x1111) {
                        result = 17;
                        if (HIWORD(subsoctype_7972) != 0x2222) {
                            result = 18;
                            if (HIWORD(subsoctype_7972) != 0x4444) {
                                v3 = 20;
                                if (HIWORD(subsoctype_7972) == 21845)
                                    return 19;
                                return v3;
                            }
                        }
                    }
                }
            }
        } else if ((uint8_t)cppsr_7970 != 0x10) {
            return -1;
        }
        return result;
    default:
        return -1;
    }
    return result;
}

static const char *ingenic_cpu_name() {
    switch (get_cpu_id()) {
    case 0:
        return "T10";
    case 1:
    case 2:
        return "T10-Lite";
    case 3:
        return "T20";
    case 4:
        return "T20-Lite";
    case 5:
        return "T20-X";
    case 6:
        return "T30-Lite";
    case 7:
        return "T30-N";
    case 8:
        return "T30-X";
    case 9:
        return "T30-A";
    case 10:
        return "T30-Z";
    case 11:
        return "T21-L";
    case 12:
        return "T21-N";
    case 13:
        return "T21-X";
    case 14:
        return "T21-Z";
    case 15:
        return "T31-L";
    case 16:
        return "T31-N";
    case 17:
        return "T31-X";
    case 18:
        return "T31-A";
    case 19:
        return "T31-ZL";
    case 20:
        return "T31-ZX";
    }
    return "unknown";
}

bool ingenic_detect_cpu() {

    // chipid (serial number?)
    // 0x13540200: 0xEBD9102F
    // 0x13540204: 0x3A922801
    // 0x13540208: 0x00001456
    // result: 2f10d9eb0128923a56140000

    strcpy(chip_name, ingenic_cpu_name());
    strcpy(chip_manufacturer, VENDOR_INGENIC);
    return true;
}

static unsigned long ingenic_media_mem() {
    char buf[256];

    if (!get_regex_line_from_file("/proc/cmdline", "rmem=([0-9x]+)", buf,
                                  sizeof(buf)))
        return 0;
    return strtoul(buf, NULL, 10);
}

unsigned long ingenic_totalmem(unsigned long *media_mem) {
    *media_mem = ingenic_media_mem();
    return kernel_mem();
}

float ingenic_get_temp() {
    float ret = -237.0;
    char buf[16];
    if (get_regex_line_from_file("/sys/class/thermal/thermal_zone0/temp",
                                 "(.+)", buf, sizeof(buf))) {
        ret = strtof(buf, NULL);
    }
    return ret;
}

void setup_hal_ingenic() {
    disable_printk();
    possible_i2c_addrs = ingenic_possible_i2c_addrs;
    if (!access("/sys/class/thermal/thermal_zone0/temp", R_OK))
        hal_temperature = ingenic_get_temp;
}
