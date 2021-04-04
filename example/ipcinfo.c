#include <getopt.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <ipchw.h>

#include "version.h"

#define RESET_CL "\x1b[0m"
#define FG_RED "\x1b[31m"

void Help() {
    printf("ipcinfo, version: ");
    const char *vers = get_git_version();
    if (*vers) {
        printf("%s\n", vers);
    } else {
        printf("%s+%s\n", get_git_branch(), get_git_revision());
    }

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
           "\t--sensor_id\n"
           "\t--temp\n"
           "\t--help\n");
}

int main(int argc, char **argv) {

    const char *short_options = "";
    const struct option long_options[] = {{"help", no_argument, NULL, 'h'},
                                          {"chip_id", no_argument, NULL, 'c'},
                                          {"sensor_id", no_argument, NULL, 's'},
                                          {"temp", no_argument, NULL, 't'},
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

        case 's': {
            const char *sensor = getsensoridentity();
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
        }
    }

    Help();
    return 0;
}
