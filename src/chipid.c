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
#include "hal/common.h"
#include "tools.h"

#define VENDOR_HISI "HiSilicon"
#define VENDOR_GOKE "Goke"

int chip_generation;
char chip_name[128];
static char chip_manufacturer[128];
char nor_chip[128];

static long get_uart0_address() {
    char buf[256];

    bool res = line_from_file("/proc/iomem",
                              "^([0-9a-f]+)-[0-9a-f]+ : .*uart[@:][0-9]", buf,
                              sizeof(buf));
    if (!res) {
        return -1;
    }
    return strtol(buf, NULL, 16);
}

typedef struct {
    const char *pattern;
    bool (*detect_fn)(char *);
    const char *override_vendor;
    void (*setup_hal_fn)(void);
} manufacturers_t;

static const manufacturers_t manufacturers[] = {
#if defined(mips) || defined(__mips__) || defined(__mips)
    {"isvp", ingenic_detect_cpu, "Ingenic", setup_hal_ingenic},
#endif
#ifdef __arm__
    {"SStar", sstar_detect_cpu, NULL, sstar_setup_hal},
    {"MStar", mstar_detect_cpu, NULL, sstar_setup_hal},
    {"Novatek", novatek_detect_cpu, NULL, novatek_setup_hal},
    {"Grain-Media", gm_detect_cpu, NULL, gm_setup_hal},
    {"FH", fh_detect_cpu, "Fullhan", fh_setup_hal},
    {NULL /* Generic */, rockchip_detect_cpu, "Rockchip", rockchip_setup_hal},
    {"Xilinx", xilinx_detect_cpu, NULL, xilinx_setup_hal},
    {"BCM", bcm_detect_cpu, "Broadcom", bcm_setup_hal},
    {NULL, allwinner_detect_cpu, "Allwinner", allwinner_setup_hal}
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
    {NULL, tegra_detect_cpu, "Nvidia", tegra_setup_hal},
#endif
};

static bool generic_detect_cpu() {
    char buf[256] = "unknown";

    strcpy(chip_name, "unknown");
    bool res = line_from_file("/proc/cpuinfo", "Hardware\\s+: ([a-zA-Z-]+)",
                              buf, sizeof(buf));
    if (!res) {
        res = line_from_file("/proc/cpuinfo", "vendor_id\\s+: (.+)", buf,
                             sizeof(buf));
    }
    strcpy(chip_manufacturer, buf);

    for (size_t i = 0; i < ARRCNT(manufacturers); i++) {
        if (manufacturers[i].pattern &&
            strncmp(manufacturers[i].pattern, chip_manufacturer,
                    strlen(manufacturers[i].pattern)))
            continue;

        if (manufacturers[i].detect_fn(chip_name)) {
            if (manufacturers[i].override_vendor)
                strcpy(chip_manufacturer, manufacturers[i].override_vendor);
            manufacturers[i].setup_hal_fn();
            return true;
        }
    }

    return false;
}

static bool detect_and_set(const char *manufacturer,
                           bool (*detect_fn)(char *, uint32_t),
                           void (*setup_hal_fn)(void), uint32_t base) {
    bool ret = detect_fn(chip_name, base);
    if (ret) {
        strcpy(chip_manufacturer, manufacturer);
        setup_hal_fn();
    }

    return ret;
}

static bool hw_detect_system() {
    long uart_base = get_uart0_address();
    switch (uart_base) {
#ifdef __arm__
    // xm510
    case 0x10030000:
        return detect_and_set("Xiongmai", xm_detect_cpu, setup_hal_xm, 0);
    // hi3516cv300
    case 0x12100000:
    // hi3516ev200
    case 0x120a0000:
    case 0x12040000: {
        int ret = detect_and_set(VENDOR_HISI, hisi_detect_cpu, setup_hal_hisi,
                                 0x12020000);
        if (ret && *chip_name == '7')
            strcpy(chip_manufacturer, VENDOR_GOKE);
        return ret;
    }
    // hi3536c
    case 0x12080000:
        return detect_and_set(VENDOR_HISI, hisi_detect_cpu, setup_hal_hisi,
                              0x12050000);
    // hi3516av100
    // hi3516cv100
    // hi3518ev200
    case 0x20080000:
        return detect_and_set(VENDOR_HISI, hisi_detect_cpu, setup_hal_hisi,
                              0x20050000);
#endif
    default:
        return generic_detect_cpu();
    }
}

static char sysid[255];
const char *getchipname() {
    // if system wasn't detected previously
    if (*sysid)
        return sysid;

    setup_hal_fallback();
    if (!hw_detect_system())
        return NULL;

    if (!strcmp(chip_manufacturer, VENDOR_HISI))
        strcpy(sysid, "hi");
    else if (!strcmp(chip_manufacturer, VENDOR_GOKE))
        strcpy(sysid, "gk");
    int nlen = strlen(sysid);
    lsnprintf(sysid + nlen, sizeof(sysid) - nlen, "%s", chip_name);

    return sysid;
}

const char *getchipfamily() {
    const char *chip_name = getchipname();
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
        if (*chip_name == 'g')
            return "gk7205v200";
        else
            return "hi3516ev200";
    case INFITITY6:
        return "infinity6";
    case INFITITY6B0:
        return "infinity6b0";
    case INFITITY6E:
        return "infinity6e";
    default:
        return chip_name;
    }
}

const char *getchipvendor() {
    getchipname();
    return chip_manufacturer;
}

#ifndef STANDALONE_LIBRARY
cJSON *detect_chip() {
    cJSON *fake_root = cJSON_CreateObject();
    cJSON *j_inner = cJSON_CreateObject();
    cJSON_AddItemToObject(fake_root, "chip", j_inner);

    ADD_PARAM("vendor", chip_manufacturer);
    ADD_PARAM("model", chip_name);
    if (hal_chip_properties)
        hal_chip_properties(j_inner);

    return fake_root;
}
#endif
