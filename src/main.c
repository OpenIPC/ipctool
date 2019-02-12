#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>

#include <dlfcn.h>

#include "chipid.h"

#define VERSION_NAME_MAXLEN 64
typedef struct hiMPP_VERSION_S {
    char aVersion[VERSION_NAME_MAXLEN];
} MPP_VERSION_S;
int (*HI_MPI_SYS_GetVersion)(MPP_VERSION_S* pstVersion);
// int (*HI_MPI_SYS_GetChipId)(MPP_VERSION_S* pstVersion);

int main(int argc, char *argv[]) {
    void *lib = dlopen("libmpi.so", RTLD_NOW | RTLD_GLOBAL);
    if (!lib) {
        printf("Can't find libmpi.so\n");
    } else {
        HI_MPI_SYS_GetVersion = dlsym(lib, "HI_MPI_SYS_GetVersion");
        if (!HI_MPI_SYS_GetVersion) {
            printf("Can't find HI_MPI_SYS_GetVersion in libmpi.so\n");
        } else {
            MPP_VERSION_S version;
            HI_MPI_SYS_GetVersion(&version);
            printf("MPP version: %s\n", version.aVersion);
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
 
    chip_id();
    isp_version();

    return EXIT_SUCCESS;
}
