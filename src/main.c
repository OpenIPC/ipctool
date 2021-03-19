#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <termios.h>
#include <unistd.h>

#include "backup.h"
#include "chipid.h"
#include "cjson/cJSON.h"
#include "cjson/cYAML.h"
#include "firmware.h"
#include "hal_hisi.h"
#include "mtd.h"
#include "network.h"
#include "ram.h"
#include "sensors.h"
#include "tools.h"
#include "version.h"

#include "vendors/openwrt.h"
#include "vendors/xm.h"

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
           "\t--dmesg\n"
           "\t--help\n");
}

// backup mode pipe end
FILE *backup_fp;

int yaml_printf(char *format, ...) {
    va_list arglist;

    va_start(arglist, format);
    int ret = vfprintf(stdout, format, arglist);
    if (backup_fp)
        vfprintf(backup_fp, format, arglist);
    va_end(arglist);
    return ret;
}

void show_yaml(cJSON *json) {
    char *string = cYAML_Print(json);
    fprintf(stdout, "%s", string);
    if (backup_fp)
        fprintf(backup_fp, "%s", string);

    cJSON_Delete(json);
}

void print_system_id() {
    if (!*system_manufacturer && !*system_id)
        return;

    yaml_printf("vendor: %s\n"
                "model: %s\n",
                system_manufacturer, system_id);
}

void print_board_id() {
    if (!*board_manufacturer && !*board_id)
        return;

    yaml_printf("board:\n");

    if (*board_manufacturer)
        yaml_printf("  vendor: %s\n", board_manufacturer);
    if (*board_id)
        yaml_printf("  model: %s\n", board_id);
    if (*board_ver)
        yaml_printf("  version: %s\n", board_ver);
    if (*board_specific)
        yaml_printf("%s", board_specific);
}

void print_chip_id() {
    yaml_printf("chip:\n"
                "  vendor: %s\n"
                "  model: %s\n",
                chip_manufacturer, chip_id);
    if (chip_generation == 0x3516E300) {
        char buf[1024];
        if (hisi_ev300_get_die_id(buf, sizeof buf)) {
            yaml_printf("  id: %s\n", buf);
        }
    }
}

void print_ethernet_data() {
    char buf[1024];

    // himii: probed
    // [    1.160082] CONFIG_HIETH_PHYID_U 1
    // [    1.168332] CONFIG_HIETH_PHYID_U 1
    // [    1.172163] CONFIG_HIETH_PHYID_D 3
    yaml_printf("ethernet:\n");
    if (get_mac_address(buf, sizeof buf)) {
        yaml_printf("  mac: \"%s\"\n", buf);
    };

    uint32_t mdio_phyaddr = 0;
    switch (chip_generation) {
    case 0x35180100:
    case 0x3518E200:
        // 0x1009_0108 UD_MDIO_PHYADDR
        mdio_phyaddr = 0x10090108;
        break;
    case 0x3516C300:
        // 0x10050108 UD_MDIO_PHYADDR
        mdio_phyaddr = 0x10050108;
        break;
    }

    if (mdio_phyaddr) {
        uint32_t val;
        if (mem_reg(mdio_phyaddr, &val, OP_READ)) {
            yaml_printf("  phyaddr: %x\n", val);
            // yaml_printf("  connection: rmii\n");
        }
    }
}

void print_sensor_id() {
    yaml_printf("sensors:\n"
                "  - vendor: %s\n"
                "    model: %s\n"
                "    control:\n"
                "      bus: 0\n"
                "      type: %s\n",
                sensor_manufacturer, sensor_id, control);

    const char *data_type = get_sensor_data_type();
    if (data_type) {
        yaml_printf("    data:\n"
                    "      type: %s\n",
                    data_type);
    }

    const char *sensor_clock = get_sensor_clock();
    if (sensor_clock) {
        yaml_printf("    clock: %s\n", sensor_clock);
    }
}

bool get_board_id() {
    if (is_xm_board()) {
        gather_xm_board_info();
        return true;
    } else if (is_openwrt_board()) {
        gather_openwrt_board_info();
        return true;
    }
    return false;
}

void print_ram_info() {
    if (strlen(ram_specific)) {
        yaml_printf("ram:\n%s", ram_specific);
    }
}

static void generic_system_data() { linux_mem(); }

#define MAX_YAML 1024 * 64
static bool backup_mode() {
    // prevent double backup creation and don't backup OpenWrt firmware
    if (!udp_lock() || is_openwrt_board())
        return false;

    int fds[2];
    if (pipe(fds) == -1) {
        fprintf(stderr, "Pipe Failed");
        exit(1);
    }

    pid_t p = fork();
    if (p < 0) {
        exit(1);
    } else if (p > 0) {
        // parent process
        close(fds[0]);
        backup_fp = fdopen(fds[1], "w");
        return false;
    } else {
        close(fds[1]);
        char *yaml = calloc(MAX_YAML, 1);
        char *ptr = yaml, *end = yaml + MAX_YAML;
        size_t n;
        while ((n = read(fds[0], ptr, 10)) && ptr != end) {
            ptr += n;
        }
        size_t yaml_sz = ptr - yaml;
        close(fds[0]);
        do_backup(yaml, yaml_sz);
        free(yaml);
        return true;
    }
}

int main(int argc, char *argv[]) {
    isp_register = -1;
    sprintf(isp_version, "error");
    sprintf(isp_build_number, "error");
    sprintf(isp_sequence_number, "error");

    if (argc == 1) {
        if (backup_mode())
            return EXIT_SUCCESS;

        generic_system_data();
        if (get_system_id()) {
            yaml_printf("---\n");
            if (get_board_id()) {
                print_board_id();
            }
            print_system_id();
            print_chip_id();
            print_ethernet_data();
            print_mtd_info();
            print_ram_info();
            show_yaml(detect_firmare());
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
        } else if (strcmp(cmd, "--dmesg") == 0) {
            dmesg();
        } else
            Help();
        return EXIT_SUCCESS;
    }

    Help();
    return EXIT_SUCCESS;
}
