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
int chip_generation;
char chip_id[128];
char chip_manufacturer[128];
char short_manufacturer[128];
int isp_register = -1;
char isp_version[128];
char isp_build_number[128];
char isp_sequence_number[128];
char mpp_info[1024];

// avoid warnings for old compilers
#if __GNUC__ < 7
extern __ssize_t getline(char **__restrict __lineptr, size_t *__restrict __n,
                         FILE *__restrict __stream) __wur;
#endif

bool get_regex_line_from_file(const char *filename, const char *re, char *buf,
                              size_t buflen) {
    long res = false;

    FILE *fiomem = fopen(filename, "r");
    if (!fiomem)
        return false;

    regex_t regex;
    regmatch_t matches[2];
    if (!compile_regex(&regex, re))
        goto exit;

    char *line = buf;
    size_t len = buflen;
    ssize_t read;

    while ((read = getline(&line, &len, fiomem)) != -1) {
        if (regexec(&regex, line, sizeof(matches) / sizeof(matches[0]),
                    (regmatch_t *)&matches, 0) == 0) {
            regoff_t start = matches[1].rm_so;
            regoff_t end = matches[1].rm_eo;

            line[end] = 0;
            if (start) {
                memmove(line, line + start, end - start + 1);
            }
            res = true;
            break;
        }
    }

exit:
    regfree(&regex);
    fclose(fiomem);
    return res;
}

long get_uart0_address() {
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
        chip_generation = 0x3516C300;
        return "3516CV300";
    case 0x3516D300:
        return "3516DV300";
    case 0x3516E200:
        chip_generation = 0x3516E300;
        return "3516EV200";
    case 0x3516E300:
        chip_generation = 0x3516E300;
        return "3516EV300";
    case 0x35180100:
        chip_generation = 0x35180100;
        return get_chip_id35180100();
    case 0x3518E200:
        chip_generation = 0x3518E200;
        return "3518EV200";
    case 0x3559A100:
        return "3559AV100";
    case 0x35210100:
        return "3521V100";
    default:
        fprintf(stderr, "get_chip_id() got unexpected 0x%x\n", reg);
        return "unknown";
    }
}

bool detect_xm510() {
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

bool detect_system() {
    uint32_t SC_CTRL_base;

    long uart_base = get_uart0_address();
    switch (uart_base) {
    // xm510
    case 0x10030000:
        return detect_xm510();
    // hi3516cv300
    case 0x12100000:
    // hi3516ev200
    case 0x12040000:
        SC_CTRL_base = 0x12020000;
        break;
    // hi3518ev200
    default:
        SC_CTRL_base = 0x20050000;
    }

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
        printf("sc_ctrl_map mmap error %p\n", (int*)sc_ctrl_map);
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
    if (chip_id_u32 == 0x3518E200 || chip_id_u32 == 0x3516C300) {
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
        if (chip_id_u32 == 0x3516C300)
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

bool get_system_id() {
    if (!detect_system()) {
        return false;
    };
    setup_hal_drivers();
    return true;
}

int get_isp_version() {
    int mem_fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (mem_fd < 0) {
        printf("can't open /dev/mem \n");
        return EXIT_FAILURE;
    }

    const uint32_t base_address = 0x20580000;
    void *isp_version_map =
        mmap(NULL,                       // Any adddress in our space will do
             0x20080 + sizeof(uint32_t), // Map length
             PROT_READ,   // Enable reading & writting to mapped memory
             MAP_PRIVATE, // Shared with other processes
             mem_fd,      // File to map
             base_address // Offset to base address
        );
    close(mem_fd);
    if (isp_version_map == MAP_FAILED) {
        printf("isp_version_map mmap error %p\n", (int*)isp_version_map);
        printf("Error: %s (%d)\n", strerror(errno), errno);
        return EXIT_FAILURE;
    }

    isp_register = ((volatile uint32_t *)(isp_version_map + 0x20080))[0];
    // printf("ISP version register: 0x%08X", isp_register);

    // 0b_0000_0000_0001_0000_0000_0000_0000_0000
    // 0b_1111_1111_1111_0000_0000_0000_0000_0000
    // 0b_0000_0000_0000_1111_0000_0000_0000_0000
    // 0b_0000_0000_0000_0000_1111_1111_1111_1111

    uint32_t version =
        (isp_register & 0b11111111111100000000000000000000) >> 20;
    uint32_t build = (isp_register & 0b00000000000011110000000000000000) >>
                     16; // 0b00000000_00001111_00000000_00000000
    uint32_t sn =
        isp_register &
        0b00000000000000001111111111111111; // 0b00000000_00000000_11111111_11111111
    //    printf("    version: V%d", version*100);
    //    printf("    build: B%02d", build);
    //    printf("    sequence number: %d\n", sn);

    sprintf(isp_version, "V%d", version * 100);
    sprintf(isp_build_number, "B%02d", build);
    sprintf(isp_sequence_number, "%d", sn);

    return EXIT_SUCCESS;
}

#define VERSION_NAME_MAXLEN 64
typedef struct hiMPP_VERSION_S {
    char aVersion[VERSION_NAME_MAXLEN];
} MPP_VERSION_S;
int (*HI_MPI_SYS_GetVersion)(MPP_VERSION_S *pstVersion);
int (*HI_MPI_SYS_GetChipId)(uint32_t *chipId);

int get_mpp_info() {
    void *lib = dlopen("libmpi.so", RTLD_LAZY);
    if (!lib) {
        printf("Can't dlopen libmpi.so: %s\n", dlerror());
        return EXIT_FAILURE;
    } else {
        HI_MPI_SYS_GetVersion = dlsym(lib, "HI_MPI_SYS_GetVersion");
        if (!HI_MPI_SYS_GetVersion) {
            printf("Can't find HI_MPI_SYS_GetVersion in libmpi.so\n");
        } else {
            MPP_VERSION_S version;
            if (!HI_MPI_SYS_GetVersion(&version)) {
                char *srcptr = version.aVersion;
                char *dstptr = mpp_info;
                char *endbuf = mpp_info + sizeof mpp_info;
                bool value_part = false;
                while (endbuf != srcptr) {
                    *dstptr = *srcptr;
                    if (*srcptr == '=' && !value_part) {
                        value_part = true;
                        *++dstptr = '"';
                    }
                    if (!*srcptr)
                        break;
                    srcptr++;
                    dstptr++;
                }
                strncat(mpp_info, "\"\n",
                        sizeof(mpp_info) - strlen(mpp_info) - 1);
            };
        }

        HI_MPI_SYS_GetChipId = dlsym(lib, "HI_MPI_SYS_GetChipId");
        if (!HI_MPI_SYS_GetChipId) {
            printf("Can't find HI_MPI_SYS_GetChipId in libmpi.so\n");
        } else {
            uint32_t chipId;
            if (!HI_MPI_SYS_GetChipId(&chipId)) {
                char buf[BUFSIZ];
                snprintf(buf, sizeof buf, "HI_CHIPID=%#X", chipId);
                strncat(mpp_info, buf, sizeof(mpp_info) - strlen(mpp_info) - 1);
            };
        }
        dlclose(lib);
    }
    return EXIT_SUCCESS;
}
