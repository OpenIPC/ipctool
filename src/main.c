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
#include "mtd.h"
#include "network.h"
#include "sensors.h"
#include "tools.h"
#include "version.h"

void Help() {
    printf("ipc_chip_info, version: ");
    const char *vers = get_git_version();
    if (*vers) {
        printf("%s\n", vers);
    } else {
        printf("%s+%s\n", get_git_branch(), get_git_revision());
    }

    printf("available options:\n"
           "\t--chip_id\n"
           "\t--sensor_id\n"
           "\t--isp_register\n"
           "\t--isp_version\n"
           "\t--isp_build\n"
           "\t--isp_sequence_number\n"
           "\t--mpp_info\n"
           "\t--temp\n"
           "\t--help\n");
}

void print_system_id() {
    if (!*system_manufacturer && !*system_id)
        return;

    printf("vendor: %s\n"
           "model: %s\n",
           system_manufacturer, system_id);
}

void print_chip_id() {
    printf("chip:\n"
           "  vendor: %s\n"
           "  model: %s\n",
           chip_manufacturer, chip_id);
    if (chip_generation == 0x3516E300) {
        char buf[1024];
        if (hisi_ev300_get_die_id(buf, sizeof buf)) {
            printf("  id: %s\n", buf);
        }
    }
}

void print_ethernet_data() {
    char buf[1024];

    // himii: probed
    // [    1.160082] CONFIG_HIETH_PHYID_U 1
    // [    1.168332] CONFIG_HIETH_PHYID_U 1
    // [    1.172163] CONFIG_HIETH_PHYID_D 3
    printf("ethernet:\n");
    if (get_mac_address(buf, sizeof buf)) {
        printf("  mac: \"%s\"\n", buf);
    };

    // CV300 only
    // uint32_t val;
    // bool res;
    // res = read_mem_reg(0x10050108, &val); // 0x10050108 UD_MDIO_PHYADDR
    // printf("  phyaddr: %x\n", val);
    // printf("  connection: rmii\n");
}

void print_sensor_id() {
    printf("sensors:\n"
           "  - vendor: %s\n"
           "    model: %s\n"
           "    control:\n"
           "      bus: 0\n"
           "      type: %s\n",
           sensor_manufacturer, sensor_id, control);

    const char *data_type = get_sensor_data_type();
    if (data_type) {
        printf("    data:\n"
               "      type: %s\n",
               data_type);
    }

    const char *sensor_clock = get_sensor_clock();
    if (sensor_clock) {
        printf("    clock: %s\n", sensor_clock);
    }
}

int main(int argc, char *argv[]) {
    isp_register = -1;
    sprintf(isp_version, "error");
    sprintf(isp_build_number, "error");
    sprintf(isp_sequence_number, "error");

    if (argc == 1) {
        if (get_system_id()) {
            printf("---\n");
            print_system_id();
            print_chip_id();
            print_ethernet_data();
            print_mtd_info();
        } else
            return EXIT_FAILURE;

        // flush stdout before go to sensor detection to avoid buggy kernel
        // freezes
        tcdrain(STDOUT_FILENO);
        if (get_sensor_id())
            print_sensor_id();
        else
            return EXIT_FAILURE;

        return EXIT_SUCCESS;
    }

    if (argc == 2) {
        char *cmd = argv[1];
        if (strcmp(cmd, "--chip_id") == 0) {
            if (get_system_id())
                lprintf("%s%s\n", short_manufacturer, chip_id);
            else
                return EXIT_FAILURE;
        } else if (strcmp(cmd, "--sensor_id") == 0) {
            if (get_sensor_id())
                lprintf("%s_%s\n", sensor_id, control);
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
        } else if (strcmp(cmd, "--mpp_info") == 0) {
            if (get_mpp_info() == EXIT_SUCCESS)
                printf("%s\n", mpp_info);
            else
                return EXIT_FAILURE;
        } else if (strcmp(cmd, "--temp") == 0) {
            get_system_id();
            return hisi_get_temp();
        } else
            Help();
        return EXIT_SUCCESS;
    }

    Help();
    return EXIT_SUCCESS;
}
