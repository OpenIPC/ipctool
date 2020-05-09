#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <termios.h>
#include <unistd.h>

#include "chipid.h"
#include "hal_hisi.h"
#include "sensorid.h"
#include "version.h"

void Help() {
    printf("ipc_chip_info, version: ");
    const char *vers = get_git_version();
    if (*vers) {
        printf("%s\n", vers);
    } else {
        printf("%s+%s\n", get_git_branch(), get_git_revision());
    }

    printf("    available options:\n");
    printf("        --system_id\n");
    printf("        --chip_id\n");
    printf("        --sensor_id\n");
    printf("        --isp_register\n");
    printf("        --isp_version\n");
    printf("        --isp_build\n");
    printf("        --isp_sequence_number\n");
    printf("        --mpp_version\n");
    printf("        --help\n");
}

void print_system_id(bool report) {
    if (!*system_manufacturer && !*system_id)
        return;

    if (*system_manufacturer) {
        strcat(system_manufacturer, " ");
    }
    printf("System: %s%s\n", system_manufacturer, system_id);
}

void print_chip_id(bool report) {
    if (*chip_manufacturer) {
        strcat(chip_manufacturer, " ");
    }
    printf("Chip: %s%s\n", chip_manufacturer, chip_id);
}

void print_sensor_id(bool report) {
    if (*sensor_manufacturer) {
        strcat(sensor_manufacturer, " ");
    }
    printf("Sensor: %s%s\n", sensor_manufacturer, sensor_id);
}

int main(int argc, char *argv[]) {
    isp_register = -1;
    sprintf(isp_version, "error");
    sprintf(isp_build_number, "error");
    sprintf(isp_sequence_number, "error");
    sprintf(mpp_version, "error");

    if (argc == 1) {
        if (get_system_id()) {
            print_system_id(true);
            print_chip_id(true);
        } else
            return EXIT_FAILURE;

        // flush stdout before go to sensor detection to avoid buggy kernel
        // freezes
        tcdrain(STDOUT_FILENO);
        if (get_sensor_id())
            print_sensor_id(true);
        else
            return EXIT_FAILURE;

        return EXIT_SUCCESS;
    }

    if (argc == 2) {
        char *cmd = argv[1];
        if (strcmp(cmd, "--system_id") == 0) {
            if (get_system_id())
                printf("%s\n", system_id);
            else
                return EXIT_FAILURE;
        } else if (strcmp(cmd, "--chip_id") == 0) {
            if (get_system_id())
                printf("%s\n", chip_id);
            else
                return EXIT_FAILURE;
        } else if (strcmp(cmd, "--sensor_id") == 0) {
            if (get_sensor_id())
                printf("%s\n", sensor_id);
            else
                return EXIT_FAILURE;
        } else if (strcmp(cmd, "--isp_register") == 0) {
            if (get_isp_version() == EXIT_SUCCESS)
                printf("0x%08X\n", isp_register);
            else
                return EXIT_FAILURE;
        } else if (strcmp(cmd, "--isp_version") == 0) {
            if (get_isp_version() == EXIT_SUCCESS)
                printf("%s\n", isp_version);
            else
                return EXIT_FAILURE;
        } else if (strcmp(cmd, "--isp_build") == 0) {
            if (get_isp_version() == EXIT_SUCCESS)
                printf("%s\n", isp_build_number);
            else
                return EXIT_FAILURE;
        } else if (strcmp(cmd, "--isp_sequence_number") == 0) {
            if (get_isp_version() == EXIT_SUCCESS)
                printf("%s\n", isp_sequence_number);
            else
                return EXIT_FAILURE;
        } else if (strcmp(cmd, "--mpp_version") == 0) {
            if (get_mpp_version() == EXIT_SUCCESS)
                printf("%s\n", mpp_version);
            else
                return EXIT_FAILURE;
        } else
            Help();
        return EXIT_SUCCESS;
    }

    Help();
    return EXIT_SUCCESS;
}
