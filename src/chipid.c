#include "chipid.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <dlfcn.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

char system_id[128];
char chip_id[128];
int  isp_register = -1;
char isp_version[128];
char isp_build_number[128];
char isp_sequence_number[128];
char mpp_version[128];

int get_chip_id() {
    int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0) { printf("can't open /dev/mem \n"); return EXIT_FAILURE; }

    uint32_t SC_CTRL_base = 0x20050000;
    uint32_t SCSYSID0 = 0xEE0;
    uint32_t SCSYSID1 = 0xEE4;
    uint32_t SCSYSID2 = 0xEE8;
    uint32_t SCSYSID3 = 0xEEC;

//    uint32_t base = SC_CTRL_base + SCSYSID0;
//    long page_size = sysconf(_SC_PAGE_SIZE);
//    off_t page_base = (base / page_size) * page_size;
//    off_t page_offset = base - page_base;
    volatile char *sc_ctrl_map = mmap(
        NULL,                  // Any adddress in our space will do
        SCSYSID0 + 4*100,   // Map length
        PROT_READ,             // Enable reading & writting to mapped memory
        MAP_SHARED,            // Shared with other processes
        mem_fd,                // File to map
        SC_CTRL_base                   // Offset to base address
    );
    if (sc_ctrl_map == MAP_FAILED) { printf("sc_ctrl_map mmap error %d\n", (int)sc_ctrl_map);
        printf("Error: %s (%d)\n", strerror(errno), errno); close(mem_fd); return EXIT_FAILURE; }

    close(mem_fd);

    uint32_t chip_id_u32 = 0;
    char* ptr = (char*)&chip_id_u32;
    ptr[0] = *(volatile char *)(sc_ctrl_map + SCSYSID0);
    ptr[1] = *(volatile char *)(sc_ctrl_map + SCSYSID1);
    ptr[2] = *(volatile char *)(sc_ctrl_map + SCSYSID2);
    ptr[3] = *(volatile char *)(sc_ctrl_map + SCSYSID3);
    sprintf(system_id, "%x", chip_id_u32);
    // printf("System id: %X", chip_id_u32);

    uint32_t SCSYSID0_reg = ((volatile uint32_t *)(sc_ctrl_map + SCSYSID0))[0];
    char SCSYSID0_chip_id = ((char*)&SCSYSID0_reg)[3];
    // printf("    SCSYSID0 chip_id: ");
    switch (SCSYSID0_chip_id) {
        case 1: sprintf(chip_id, "hi3516cv200"); break;
        case 2: sprintf(chip_id, "hi3518ev200"); break;
        case 3: sprintf(chip_id, "hi3518ev201"); break;
        default:
            sprintf(chip_id, "reserved value %d", SCSYSID0_chip_id);
    }

    return EXIT_SUCCESS;
}

int get_isp_version() {
    int mem_fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (mem_fd < 0) { printf("can't open /dev/mem \n"); return EXIT_FAILURE; }

    const uint32_t base_address = 0x20580000;
    void *isp_version_map = mmap(
        NULL,                       // Any adddress in our space will do
        0x20080 + sizeof(uint32_t),           // Map length
        PROT_READ,                  // Enable reading & writting to mapped memory
        MAP_PRIVATE,                // Shared with other processes
        mem_fd,                     // File to map
        base_address      // Offset to base address
    );
    close(mem_fd);
    if (isp_version_map == MAP_FAILED) { printf("isp_version_map mmap error %d\n", (int)isp_version_map);
        printf("Error: %s (%d)\n", strerror(errno), errno); return EXIT_FAILURE; }

    isp_register = ((volatile uint32_t *)(isp_version_map + 0x20080))[0];
    // printf("ISP version register: 0x%08X", isp_register);

    // 0b_0000_0000_0001_0000_0000_0000_0000_0000
    // 0b_1111_1111_1111_0000_0000_0000_0000_0000
    // 0b_0000_0000_0000_1111_0000_0000_0000_0000
    // 0b_0000_0000_0000_0000_1111_1111_1111_1111

    uint32_t version = (isp_register & 0b11111111111100000000000000000000) >> 20;
    uint32_t build   = (isp_register & 0b00000000000011110000000000000000) >> 16;   // 0b00000000_00001111_00000000_00000000
    uint32_t sn      =  isp_register & 0b00000000000000001111111111111111;          // 0b00000000_00000000_11111111_11111111
//    printf("    version: V%d", version*100);
//    printf("    build: B%02d", build);
//    printf("    sequence number: %d\n", sn);

    sprintf(isp_version, "V%d", version*100);
    sprintf(isp_build_number, "B%02d", build);
    sprintf(isp_sequence_number, "%d", sn);

    return EXIT_SUCCESS;
}

#define VERSION_NAME_MAXLEN 64
typedef struct hiMPP_VERSION_S {
    char aVersion[VERSION_NAME_MAXLEN];
} MPP_VERSION_S;
int (*HI_MPI_SYS_GetVersion)(MPP_VERSION_S* pstVersion);
// int (*HI_MPI_SYS_GetChipId)(MPP_VERSION_S* pstVersion);

int get_mpp_version() {
    void *lib = dlopen("libmpi.so", RTLD_NOW | RTLD_GLOBAL);
    if (!lib) {
        printf("Can't find libmpi.so\n");
        return EXIT_FAILURE;
    } else {
        HI_MPI_SYS_GetVersion = dlsym(lib, "HI_MPI_SYS_GetVersion");
        if (!HI_MPI_SYS_GetVersion) {
            printf("Can't find HI_MPI_SYS_GetVersion in libmpi.so\n");
            return EXIT_FAILURE;
        } else {
            MPP_VERSION_S version;
            HI_MPI_SYS_GetVersion(&version);
            // printf("MPP version: %s\n", version.aVersion);
            sprintf(mpp_version, "%s", version.aVersion);
        }

        // HI_MPI_SYS_GetChipId = dlsym(lib, "HI_MPI_SYS_GetChipId");
        // if (!HI_MPI_SYS_GetChipId) {
        //     printf("Can't find HI_MPI_SYS_GetChipId in libmpi.so\n");
        // } else {
        //     MPP_VERSION_S version;
        //     HI_MPI_SYS_GetChipId(&version);
        //     printf("HI_MPI_SYS_GetChipId version: '%s'\n", version.aVersion);
        // }
        dlclose(lib);
    }
    return EXIT_SUCCESS;
}
