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
#include <sys/mman.h>
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
char chip_id[128];
char chip_manufacturer[128];
char short_manufacturer[128];
char mpp_info[1024];
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

static const char *get_chip_id35180100() {
    int dvrid = hisi_SYS_DRV_GetChipId();
    switch (dvrid) {
    case 0x3516C100:
        return "3516CV100";
    case 0x3518E100:
        return "3518EV100";
    case 0x3518A100:
        return "3518AV100";
    default:
        fprintf(stderr,
                "get_chip_id35180100() got unexpected 0x%x for 3518?v100\n"
                "Check kernel modules loaded\n",
                dvrid);
        return "unknown";
    }
}

static const char *get_hisi_chip_id(uint32_t reg) {
    switch (reg) {
    case 0x6000001:
        return "3516AV200";
    case 0x3516A100:
        return "3516AV100";
    case 0x3516D100:
        return "3516DV100";
    case 0x3516A200:
        return "3516AV200";
    case 0x35190101:
        return "3519V101";
    case 0x3516C300:
        chip_generation = HISI_V3;
        return "3516CV300";
    case 0x3516D300:
        return "3516DV300";
    case 0x3516E200:
        chip_generation = HISI_V4;
        return "3516EV200";
    case 0x3516E300:
        chip_generation = HISI_V4;
        return "3516EV300";
    case 0x35180100:
        chip_generation = HISI_V1;
        return get_chip_id35180100();
    case 0x3518E200:
        chip_generation = HISI_V2;
        return "3518EV200";
    case 0x3536D100:
        chip_generation = HISI_V2;
        return "3536DV100";
    case 0x3520D100:
        return "3520DV200";
    case 0x35210100:
        return "3521V100";
    case 0x3559A100:
        return "3559AV100";
    case 0xbda9d100:
        chip_generation = HISI_V3;
        return "3536CV100";
    default:
        fprintf(stderr, "get_chip_id() got unexpected 0x%x\n", reg);
        return "unknown";
    }
}

static bool detect_xiongmai() {
    char buf[256];

    bool res = get_regex_line_from_file("/proc/cpuinfo", "^Hardware.+(xm.+)",
                                        buf, sizeof(buf));
    if (!res) {
        return false;
    }
    strncpy(chip_id, buf, sizeof(chip_id));
    char *ptr = chip_id;
    while (*ptr) {
        *ptr = toupper(*ptr);
        ptr++;
    }
    strcpy(chip_manufacturer, VENDOR_XM);
    return true;
}

static bool detect_generic() {
    char buf[256];

    bool res = get_regex_line_from_file("/proc/cpuinfo", "Hardware.+: (\\w+)",
                                        buf, sizeof(buf));
    if (!res) {
        return false;
    }
    strcpy(chip_manufacturer, buf);
    strcpy(chip_id, "unknown");
    return true;
}

static bool detect_hisilicon(uint32_t SC_CTRL_base) {
    int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0) {
        printf("can't open /dev/mem \n");
        return false;
    }

    uint32_t SCSYSID0 = 0xEE0;
    uint32_t SCSYSID1 = 0xEE4;
    uint32_t SCSYSID2 = 0xEE8;
    uint32_t SCSYSID3 = 0xEEC;

    volatile char *sc_ctrl_map =
        mmap(NULL,               // Any adddress in our space will do
             SCSYSID0 + 4 * 100, // Map length
             PROT_READ,          // Enable reading & writting to mapped memory
             MAP_SHARED,         // Shared with other processes
             mem_fd,             // File to map
             SC_CTRL_base        // Offset to base address
        );
    if (sc_ctrl_map == MAP_FAILED) {
        printf("sc_ctrl_map mmap error %p\n", (int *)sc_ctrl_map);
        printf("Error: %s (%d)\n", strerror(errno), errno);
        close(mem_fd);
        return false;
    }

    close(mem_fd);

    uint32_t chip_id_u32 = 0;
    chip_id_u32 = *(volatile uint32_t *)(sc_ctrl_map + SCSYSID0);

    if ((chip_id_u32 >> 16 & 0xff) == 0) {
        // fallback for 8-bit registers on old platforms
        char *ptr = (char *)&chip_id_u32;
        ptr[0] = *(volatile char *)(sc_ctrl_map + SCSYSID0);
        ptr[1] = *(volatile char *)(sc_ctrl_map + SCSYSID1);
        ptr[2] = *(volatile char *)(sc_ctrl_map + SCSYSID2);
        ptr[3] = *(volatile char *)(sc_ctrl_map + SCSYSID3);
    }
    strncpy(chip_id, get_hisi_chip_id(chip_id_u32), sizeof(chip_id));

    // Special case for 16cv200/18ev200/18ev201 family
    if (chip_id_u32 == HISI_V2 || chip_id_u32 == HISI_V3) {
        uint32_t SCSYSID0_reg =
            ((volatile uint32_t *)(sc_ctrl_map + SCSYSID0))[0];
        char SCSYSID0_chip_id = ((char *)&SCSYSID0_reg)[3];
        if (chip_id_u32 == 0x3518E200)
            switch (SCSYSID0_chip_id) {
            case 1:
                sprintf(chip_id, "3516CV200");
                break;
            case 2:
                sprintf(chip_id, "3518EV200");
                break;
            case 3:
                sprintf(chip_id, "3518EV201");
                break;
            default:
                sprintf(chip_id, "reserved value %d", SCSYSID0_chip_id);
            }
        if (chip_id_u32 == HISI_V3)
            switch (SCSYSID0_chip_id) {
            case 0:
                sprintf(chip_id, "3516CV300");
                break;
            case 4:
                sprintf(chip_id, "3516EV100");
                break;
            default:
                sprintf(chip_id, "reserved value %d", SCSYSID0_chip_id);
            }
    }

    strcpy(chip_manufacturer, VENDOR_HISI);
    return true;
}

static bool hw_detect_system() {
    long uart_base = get_uart0_address();
    switch (uart_base) {
    // xm510
    case 0x10030000:
        return detect_xiongmai();
    // hi3516cv300
    case 0x12100000:
    // hi3516ev200
    case 0x12040000:
        return detect_hisilicon(0x12020000);
    // hi3536c
    case 0x12080000:
        return detect_hisilicon(0x12050000);
        break;
    // hi3516cv100
    // hi3518ev200
    case 0x20080000:
        return detect_hisilicon(0x20050000);
        break;
    default:
        return detect_generic();
    }
}

static char sysid[255];
const char *getchipid() {
    // if system wasn't detected previously
    if (*sysid)
        return sysid;

    if (!hw_detect_system())
        return NULL;
    setup_hal_drivers();
    lsnprintf(sysid, sizeof(sysid), "%s%s", short_manufacturer, chip_id);
    return sysid;
}
