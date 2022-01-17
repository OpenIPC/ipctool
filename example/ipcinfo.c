#include <assert.h>
#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <ipchw.h>

#include "version.h"

#define RESET_CL "\x1b[0m"
#define FG_RED "\x1b[31m"

void Help() {
#ifndef SKIP_VERSION
    printf("ipcinfo, version: ");
    const char *vers = get_git_version();
    if (*vers) {
        puts(vers);
    } else {
        printf("%s+%s\n", get_git_branch(), get_git_revision());
    }
#endif

    printf("\nOpenIPC is " FG_RED "asking for your help " RESET_CL
           "to support development cost and long-term maintenance of what we "
           "believe will serve a fundamental role in the advancement of a "
           "stable, flexible and most importantly, Open IP Network Camera "
           "Framework for users worldwide.\n\n"
           "Your contribution will help us " FG_RED
           "advance development proposals forward" RESET_CL
           ", and interact with the community on a regular basis.\n\n"
           "  https://openipc.org/contribution/\n\n"
           "available options:\n"
           "\t--chip_id\n"
           "\t--family\n"
           "\t--long_sensor\n"
           "\t--short_sensor\n"
           "\t--temp\n"
           "\t--xm_mac\n"
           "\t--help\n");
}

static bool try_for_xmmac(int i, size_t size) {
    char filepath[80];

    snprintf(filepath, sizeof(filepath), "/dev/mtdblock%d", i);
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        printf("\n\"%s \" could not open\n", filepath);
        return false;
    }

    const char *part = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (part == MAP_FAILED) {
        printf("Mapping Failed\n");
        return false;
    }

    for (size_t off = 0xfc00; off < size; off += 0x10000) {
        const char *ptr = part + off;

        uint16_t header = *(uint16_t *)ptr;
        if (header != 0xd4d2)
            continue;

        uint8_t mac[6] = {ptr[0x379 + 0] - 1, ptr[0x37b] - 3, ptr[0x37d] - 5,
                          ptr[0x37f] - 7,     ptr[0x381] - 9, ptr[0x383] - 11};

        printf("%02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2],
               mac[3], mac[4], mac[5]);
        return true;
    }

    close(fd);

    return false;
}

static int xm_mac() {
    FILE *fp;
    char dev[80], name[80];
    int i, es, ee;

    if ((fp = fopen("/proc/mtd", "r"))) {
        while (fgets(dev, sizeof dev, fp)) {
            name[0] = 0;
            if (sscanf(dev, "mtd%d: %x %x \"%64[^\"]\"", &i, &es, &ee, name)) {
                if (try_for_xmmac(i, es))
                    return EXIT_SUCCESS;
            }
        }
        fclose(fp);
    }
    return EXIT_FAILURE;
}

int main(int argc, char **argv) {

    const char *short_options = "";
    const struct option long_options[] = {
        {"chip_id", no_argument, NULL, 'c'},
        {"family", no_argument, NULL, 'f'},
        {"help", no_argument, NULL, 'h'},
        {"long_sensor", no_argument, NULL, 'l'},
        {"short_sensor", no_argument, NULL, 's'},
        {"temp", no_argument, NULL, 't'},
        {"xm_mac", no_argument, NULL, 'x'},
        {NULL, 0, NULL, 0}};

    int rez;
    int option_index;
    while ((rez = getopt_long_only(argc, argv, short_options, long_options,
                                   &option_index)) != -1) {

        switch (rez) {
        case 'h':
            Help();
            return 0;

        case 'c': {
            const char *chipid = getchipid();
            if (!chipid)
                return EXIT_FAILURE;
            puts(chipid);
            return EXIT_SUCCESS;
        }

        case 'f': {
            puts(getchipfamily());
            return EXIT_SUCCESS;
        }

        case 'l': {
            const char *sensor = getsensoridentity();
            if (!sensor)
                return EXIT_FAILURE;
            puts(sensor);
            return EXIT_SUCCESS;
        }

        case 's': {
            const char *sensor = getsensorshort();
            if (!sensor)
                return EXIT_FAILURE;
            puts(sensor);
            return EXIT_SUCCESS;
        }

        case 't': {
            float temp = gethwtemp();
            if (isnan(temp)) {
                fprintf(stderr, "Temperature cannot be retrieved\n");
                return EXIT_FAILURE;
            }
            printf("%.2f\n", temp);
            return EXIT_SUCCESS;
        }

        case 'x':
            return xm_mac();
        }
    }

    Help();
    return 0;
}
