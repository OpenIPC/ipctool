#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "chipid.h"
#include "hal_common.h"
#include "tools.h"

char system_id[128];
char system_manufacturer[128];
char board_id[128];
char board_ver[128];
char board_manufacturer[128];
char board_specific[1024];
int chip_generation;
char chip_name[128];
char chip_manufacturer[128];
char short_manufacturer[128];
char nor_chip[128];

static long get_uart0_address() {
    char buf[256];

    bool res = get_regex_line_from_file(
        "/proc/iomem", "^([0-9a-f]+)-[0-9a-f]+ : .*uart[@:][0-9]", buf,
        sizeof(buf));
    if (!res) {
        return -1;
    }
    return strtol(buf, NULL, 16);
}

static bool generic_detect_cpu() {
    char buf[256];

    bool res = get_regex_line_from_file("/proc/cpuinfo", "Hardware.+: ([a-zA-Z-]+)",
                                        buf, sizeof(buf));
    if (!res) {
        return false;
    }
    strcpy(chip_manufacturer, buf);
    if (!strcmp(chip_manufacturer, VENDOR_SSTAR))
        return sstar_detect_cpu();
    else if (!strcmp(chip_manufacturer, VENDOR_NOVATEK))
        return novatek_detect_cpu();
    else if (!strcmp(chip_manufacturer, VENDOR_GM))
        return gm_detect_cpu();
    strcpy(chip_name, "unknown");
    return true;
}

static bool hw_detect_system() {
    long uart_base = get_uart0_address();
    switch (uart_base) {
    // xm510
    case 0x10030000:
        return xm_detect_cpu();
    // hi3516cv300
    case 0x12100000:
    // hi3516ev200
    case 0x120a0000:
    case 0x12040000:
        return hisi_detect_cpu(0x12020000);
    // hi3536c
    case 0x12080000:
        return hisi_detect_cpu(0x12050000);
        break;
    // hi3516av100
    // hi3516cv100
    // hi3518ev200
    case 0x20080000:
        return hisi_detect_cpu(0x20050000);
        break;
    default:
        return generic_detect_cpu();
    }
}

static char sysid[255];
const char *getchipname() {
    // if system wasn't detected previously
    if (*sysid)
        return sysid;

    if (!hw_detect_system())
        return NULL;
    setup_hal_drivers();
    lsnprintf(sysid, sizeof(sysid), "%s%s", short_manufacturer, chip_name);
    return sysid;
}

const char *getchipfamily() {
    getchipname();
    switch (chip_generation) {
    case HISI_V1:
        return "hi3516cv100";
    case HISI_V2A:
        return "hi3516av100";
    case HISI_V2:
        return "hi3516cv200";
    case HISI_V3A:
        return "hi3519v100";
    case HISI_V3:
        return "hi3516cv300";
    case HISI_V4A:
        return "hi3516cv500";
    case HISI_V4:
        if (*chip_name == '7')
            return "gk7205v200";
        else
            return "hi3516ev300";
    default:
        return "unknown";
    }
}
