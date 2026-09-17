#include <ctype.h>
#include <errno.h>
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

int chip_generation;
char chip_name[128];
char nor_chip_name[128];
char nor_chip_id[128];
static char chip_manufacturer[128];

/* Which of the two detection paths ran, so a failure can report the input
 * that actually let us down: a known UART0 base picks a vendor detector and
 * never looks at /proc/cpuinfo, while the fallback does the opposite. */
static enum {
    DETECT_PATH_NONE,
    DETECT_PATH_UART,    /* a known UART0 base chose a vendor detector */
    DETECT_PATH_GENERIC, /* fell back to the /proc/cpuinfo table */
} detect_path;
static long detect_uart_base = -1;

static long get_uart0_address() {
    char buf[256];

    if (!line_from_file("/proc/iomem", "^(\\w+)-.+:.+uart",
                buf, sizeof(buf))) {
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
#ifdef IPCHW_VENDOR_INGENIC
    {"isvp", ingenic_detect_cpu, VENDOR_INGENIC, setup_hal_ingenic},
    {"ingenic", ingenic_detect_cpu, VENDOR_INGENIC, setup_hal_ingenic},
#endif
#endif
#ifdef __arm__
#ifdef IPCHW_VENDOR_SSTAR
    {"SStar", sstar_detect_cpu, VENDOR_SSTAR, sstar_setup_hal},
    {"MStar", mstar_detect_cpu, NULL, sstar_setup_hal},
#endif
#ifdef IPCHW_VENDOR_NOVATEK
    {"Novatek", novatek_detect_cpu, VENDOR_NOVATEK, novatek_setup_hal},
#endif
#ifdef IPCHW_VENDOR_GM
    {"Grain", gm_detect_cpu, VENDOR_GM, gm_setup_hal},
#endif
#ifdef IPCHW_VENDOR_FH
    {"FH", fh_detect_cpu, VENDOR_FH, fh_setup_hal},
#endif
#ifdef IPCHW_VENDOR_ANYKA
    {NULL /* Generic */, anyka_detect_cpu, VENDOR_ANYKA, anyka_setup_hal},
#endif
#ifdef IPCHW_VENDOR_ROCKCHIP
    {NULL /* Generic */, rockchip_detect_cpu, VENDOR_ROCKCHIP, rockchip_setup_hal},
#endif
#ifdef IPCHW_VENDOR_XILINX
    {"Xilinx", xilinx_detect_cpu, NULL, xilinx_setup_hal},
#endif
#ifdef IPCHW_VENDOR_BCM
    {"BCM", bcm_detect_cpu, VENDOR_BCM, bcm_setup_hal},
#endif
#ifdef IPCHW_VENDOR_ALLWINNER
    {NULL, allwinner_detect_cpu, VENDOR_ALLWINNER, allwinner_setup_hal},
#endif
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
#ifdef IPCHW_VENDOR_NOVATEK
    {NULL, novatek_detect_cpu, VENDOR_NOVATEK, novatek_setup_hal},
#endif
#ifdef IPCHW_VENDOR_TEGRA
    {NULL, tegra_detect_cpu, "Nvidia", tegra_setup_hal},
#endif
#endif
    /* Sentinel, so the array stays well-formed when every optional vendor is
     * compiled out; generic_detect_cpu() skips entries without a detect_fn. */
    {NULL, NULL, NULL, NULL},
};

static bool generic_detect_cpu() {
    char buf[256] = "unknown";

    detect_path = DETECT_PATH_GENERIC;

    /* Only the sentinel left: every vendor HAL this build carries was
     * compiled out for this architecture, so there is nothing the table can
     * match and the three /proc/cpuinfo reads below are pure cost -- they
     * bring in line_from_file() and with it the POSIX regex engine. The
     * count is a compile-time constant, so the rest folds away.
     *
     * Both buffers still have to be filled: getchipvendor() hands back
     * chip_manufacturer whether detection succeeded or not, so returning
     * early without it makes `ipcinfo -v` print an empty line where it used
     * to print "unknown" -- and S70vendor runs load_"$vendor". */
    if (ARRCNT(manufacturers) == 1) {
        strcpy(chip_name, "unknown");
        strcpy(chip_manufacturer, "unknown");
        return false;
    }

    strcpy(chip_name, "unknown");
    bool res = line_from_file("/proc/cpuinfo", "Hardware.+:.(\\w+)",
                buf, sizeof(buf));
    if (!res) {
        res = line_from_file("/proc/cpuinfo", "vendor_id.+:.(\\w+)",
                buf, sizeof(buf));
    }
    if (!res) {
        res = line_from_file("/proc/cpuinfo", "machine.+:.(\\w+)",
                buf, sizeof(buf));
    }
    strcpy(chip_manufacturer, buf);

    for (size_t i = 0; i < ARRCNT(manufacturers); i++) {
        if (!manufacturers[i].detect_fn)
            continue;

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
    detect_path = DETECT_PATH_UART;

    bool ret = detect_fn(chip_name, base);
    if (ret) {
        strcpy(chip_manufacturer, manufacturer);
        setup_hal_fn();
    }

    return ret;
}

static bool hw_detect_system() {
    long uart_base = get_uart0_address();

    detect_uart_base = uart_base;
    switch (uart_base) {
#ifdef IPCHW_HISI_ANY
    // hi3516cv610 (ARMv7) / hi3519dv500 (aarch64) — HiSilicon V5 family
    case 0x11040000: {
        int ret = detect_and_set(VENDOR_HISI, hisi_detect_cpu, setup_hal_hisi,
                                 0x11020000);
        return ret;
    }
#endif
#ifdef __arm__
#ifdef IPCHW_VENDOR_XM
    // xm510
    case 0x10030000:
        return detect_and_set("Xiongmai", xm_detect_cpu, setup_hal_xm, 0);
#endif
#ifdef IPCHW_HISI_ANY
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
#endif /* IPCHW_HISI_ANY */
#endif /* __arm__ */
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
#ifdef IPCHW_HISI_V1
    case HISI_V1:
        return "hi3516cv100";
#endif
#ifdef IPCHW_HISI_V2A
    case HISI_V2A:
        return "hi3516av100";
#endif
#ifdef IPCHW_HISI_V2
    case HISI_V2:
        return "hi3516cv200";
#endif
#ifdef IPCHW_HISI_V3A
    case HISI_V3A:
        return "hi3519v100";
#endif
#ifdef IPCHW_HISI_V3
    case HISI_V3:
        return "hi3516cv300";
#endif
#ifdef IPCHW_HISI_V4A
    case HISI_V4A:
        return "hi3516cv500";
#endif
#ifdef IPCHW_HISI_V4
    case HISI_V4:
        if (*chip_name == 'g')
            return "gk7205v200";
        else
            return "hi3516ev200";
#endif
#ifdef IPCHW_VENDOR_SSTAR
    case INFINITY3:
        return "infinity3";
#endif
#ifdef IPCHW_VENDOR_SSTAR
    case INFINITY5:
        return "infinity5";
#endif
#ifdef IPCHW_VENDOR_SSTAR
    case INFINITY6:
        return "infinity6";
#endif
#ifdef IPCHW_VENDOR_SSTAR
    case INFINITY6B:
        return "infinity6b0";
#endif
#ifdef IPCHW_VENDOR_SSTAR
    case INFINITY6C:
        return "infinity6c";
#endif
#ifdef IPCHW_VENDOR_SSTAR
    case INFINITY6E:
        return "infinity6e";
#endif
#ifdef IPCHW_VENDOR_INGENIC
    case T10:
        return "t10";
#endif
#ifdef IPCHW_VENDOR_INGENIC
    case T20:
        return "t20";
#endif
#ifdef IPCHW_VENDOR_INGENIC
    case T21:
        return "t21";
#endif
#ifdef IPCHW_VENDOR_INGENIC
    case T23:
        return "t23";
#endif
#ifdef IPCHW_VENDOR_INGENIC
    case T30:
        return "t30";
#endif
#ifdef IPCHW_VENDOR_INGENIC
    case T31:
        return "t31";
#endif
#ifdef IPCHW_VENDOR_INGENIC
    case T40:
        return "t40";
#endif
#ifdef IPCHW_VENDOR_INGENIC
    case T41:
        return "t41";
#endif
#ifdef IPCHW_VENDOR_ROCKCHIP
    case RV1106:
        return "rv1106";
#endif
#ifdef IPCHW_VENDOR_ANYKA
    case AK39_EV2:
        return "ak3918ev200";
    case AK39_EV3:
        return "ak3918ev300";
    case AK39_EV330:
        return "ak39ev330";
    case AK39_EV300L:
        return "ak3918ev300l";
    case AK39_AV100:
        return "ak3918av100";
    case AK37_D:
        return "ak37d";
    case AK37_E:
        return "ak37e";
#endif

    default:
        return chip_name;
    }
}

const char *getchipvendor() {
    getchipname();
    return chip_manufacturer;
}

#ifndef STANDALONE_LIBRARY
/* getchipname() failing used to be silent: every report section came out
 * empty and ipctool exited 1 without a word, which reads as a broken binary
 * (#134 on an Anyka AK3918, #138 on a desktop). Report the input detection
 * actually used -- and only that one, since a known UART0 base and the
 * /proc/cpuinfo table are alternatives, never both. */

/* -1 from get_uart0_address() covers three different situations and the
 * advice differs for each, so take them apart here rather than widen
 * line_from_file(), which half the codebase calls. */
static void explain_missing_uart0() {
    FILE *f = fopen("/proc/iomem", "r");
    if (!f) {
        fprintf(stderr, "  /proc/iomem could not be read: %s\n",
                strerror(errno));
        return;
    }
    fclose(f);

    if (geteuid() != 0)
        fprintf(stderr, "  no UART0 entry in /proc/iomem, which zeroes every\n"
                        "  address unless you are root -- try again as root\n");
    else
        fprintf(stderr, "  no UART0 entry in /proc/iomem\n");
}

void explain_unknown_chip() {
    char buf[256];

    fprintf(stderr,
            "ipctool: SoC not recognised, no hardware report possible.\n");

    if (detect_path == DETECT_PATH_UART) {
        /* The UART0 base named a family, so the SoC is one ipctool knows;
         * what failed is reading its identity register. A kernel built with
         * CONFIG_STRICT_DEVMEM looks exactly like this from here. */
        fprintf(stderr,
                "  UART0 base 0x%lx in /proc/iomem is a known one, but the\n"
                "  chip did not identify itself -- reading its registers\n"
                "  through /dev/mem may be blocked (CONFIG_STRICT_DEVMEM)\n",
                detect_uart_base);
    } else {
        if (detect_uart_base != -1)
            fprintf(stderr,
                    "  UART0 base 0x%lx in /proc/iomem is not one ipctool\n"
                    "  knows\n",
                    detect_uart_base);
        else
            explain_missing_uart0();

        if (line_from_file("/proc/cpuinfo", "Hardware.+:.(\\w+)", buf,
                           sizeof(buf)) ||
            line_from_file("/proc/cpuinfo", "vendor_id.+:.(\\w+)", buf,
                           sizeof(buf)) ||
            line_from_file("/proc/cpuinfo", "machine.+:.(\\w+)", buf,
                           sizeof(buf)))
            fprintf(stderr, "  no vendor matches /proc/cpuinfo: %s\n", buf);
        else
            fprintf(stderr,
                    "  no Hardware/vendor_id/machine line in /proc/cpuinfo\n");
    }

#if defined(__i386__) || defined(__x86_64__)
    fprintf(stderr,
            "This is an x86 build. ipctool probes camera hardware\n"
            "directly, so it has to run on the camera itself, not on a\n"
            "desktop.\n");
#else
    fprintf(stderr,
            "Run it as root on the camera. If this is an IP camera or DVR,\n"
            "please report the SoC with the output of\n"
            "'cat /proc/cpuinfo; cat /proc/iomem; dmesg | head -40' at\n"
            "https://github.com/OpenIPC/ipctool/issues\n");
#endif
}

cJSON *detect_chip() {
    cJSON *j_inner = cJSON_CreateObject();

    ADD_PARAM("vendor", chip_manufacturer);
    ADD_PARAM("model", chip_name);
    if (hal_chip_properties)
        hal_chip_properties(j_inner);

    return j_inner;
}
#endif
