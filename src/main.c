#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <termios.h>

#include "chipid.h"
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

int main(int argc, char *argv[]) {
    sprintf(system_id, "error");
    sprintf(chip_id, "error");
    sprintf(sensor_id, "error");
    isp_register = -1;
    sprintf(isp_version, "error");
    sprintf(isp_build_number, "error");
    sprintf(isp_sequence_number, "error");
    sprintf(mpp_version, "error");

    if (argc == 1) {
        if (get_system_id() == EXIT_SUCCESS)
            printf("System id: %s    SCSYSID0 chip_id: %s\n", system_id,
                   chip_id);
        else
            return EXIT_FAILURE;

	// flush stdout before go to sensor detection to avoid buggy kernel
	// freezes
	tcdrain(STDOUT_FILENO);
        if (get_sensor_id() == EXIT_SUCCESS)
            printf("Sensor id: %s\n", sensor_id);
        else
            return EXIT_FAILURE;
        if (get_isp_version() == EXIT_SUCCESS)
            printf("ISP register: 0x%08X    isp version: %s    isp build: %s   "
                   " isp sequence number: %s\n",
                   isp_register, isp_version, isp_build_number,
                   isp_sequence_number);
        else
            return EXIT_FAILURE;
        if (get_mpp_version() == EXIT_SUCCESS)
            printf("MPP version: %s\n", mpp_version);
        else
            return EXIT_FAILURE;
        return EXIT_SUCCESS;
    }

    if (argc == 2) {
        char *cmd = argv[1];
        if (strcmp(cmd, "--system_id") == 0) {
            if (get_system_id() == EXIT_SUCCESS)
                printf("%s\n", system_id);
            else
                return EXIT_FAILURE;
        } else if (strcmp(cmd, "--chip_id") == 0) {
            if (get_system_id() == EXIT_SUCCESS)
                printf("%s\n", chip_id);
            else
                return EXIT_FAILURE;
        } else if (strcmp(cmd, "--sensor_id") == 0) {
            if (get_sensor_id() == EXIT_SUCCESS)
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
