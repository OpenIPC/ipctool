#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "backup.h"
#include "chipid.h"
#include "cjson/cJSON.h"
#include "cjson/cYAML.h"
#include "ethernet.h"
#include "firmware.h"
#include "hal/hisi/hal_hisi.h"
#include "hwinfo.h"
#include "i2cspi.h"
#include "mtd.h"
#include "network.h"
#include "ptrace.h"
#include "ram.h"
#include "reginfo.h"
#include "sensors.h"
#include "snstool.h"
#include "tools.h"
#include "uboot.h"
#include "version.h"
#include "watchdog.h"

#include "boards/buildroot.h"
#include "boards/common.h"

#define RESET_CL "\x1b[0m"
#define FG_RED "\x1b[31m"

void print_usage() {
#ifndef SKIP_VERSION
    printf("ipctool, version: ");

    const char *vers = get_git_version();
    if (*vers)
        printf("%s , built on %s\n", vers, get_builddate());
    else
        printf("%s+%s, built on %s\n", get_git_branch(), get_git_revision(),
               get_builddate());
#endif

#ifndef SKIP_FUNDING
    printf("\n" FG_RED "OpenIPC is asking for your help!" RESET_CL
           "\nPlease help the Team of OpenIPC project to cover the cost of "
           "development"
           "\nand long-term maintenance of what we believe will be a stable, "
           "flexible"
           "\nOpen IP Network Camera Framework for users worldwide.\n"
           "\nYour contribution could help us to advance the development and "
           "keep you"
           "\nupdated on improvements and new features more regularly.\n"
           "\nPlease visit https://openipc.org/sponsor/ to learn more. Thank "
           "you.\n\n");
#endif

    printf(
        "Usage: ipctool [OPTIONS] [COMMANDS]\n"
        "Where:\n"
        "  -c, --chip-name           read chip name\n"
        "  -s, --sensor-name         read sensor model and control line\n"
        "  -t, --temp                read chip temperature (where supported)\n"
        "\n"
        "  backup <filename>         save backup into a file\n"
        "  upload                    upload full backup to the OpenIPC cloud\n"
        "  restore [mac|filename]    restore from backup (cloud-based or local "
        "file)\n"
        "     [-s, --skip-env]       skip environment\n"
        "     [-f, --force]          enforce\n"
        "  upgrade <bundle>          upgrade to OpenIPC firmware\n"
        "                            (experimental! use only on cameras with "
        "UART)\n"
        "     [-f, --force]          enforce\n"
        "  printenv                  drop-in replacement for fw_printenv\n"
        "  setenv <key> <value>      drop-in replacement for fw_setenv\n"
        "  dmesg                     drop-in replacement for dmesg\n"
        "  i2cget <device address> <register>\n"
        "  spiget <register>\n"
        "                            read data from I2C/SPI device\n"
        "  i2cset <device address> <register> <new value>\n"
        "  spiset <register> <new value>\n"
        "                            write a value to I2C/SPI device\n"
        "  i2cdump [--script] <device address> <from register> <to register>\n"
        "  spidump [--script] <from register> <to register>\n"
        "                            dump data from I2C/SPI device\n"
        "  i2cdetect                 attempt to detect devices on I2C bus\n"
        "  reginfo [--script]        dump current status of pinmux registers\n"
        "  gpio (scan|mux)           GPIO utilities\n"
        "  trace [--skip=usleep] <full/path/to/executable> [program "
        "arguments]\n"
        "                            dump original firmware calls and data "
        "structures\n"
        "  -h, --help                this help\n");
}

void add_yaml_fragment(cJSON *root, const char *key, cJSON *json) {
    if (!json)
        return;

    // Don't show empty section
    if (!json->child) {
        cJSON_Delete(json);
        return;
    }

    cJSON_AddItemToObject(root, key, json);
}

static cJSON *build_yaml() {
    if (!getchipname()) return NULL;

    cJSON *root = cJSON_CreateObject();
    add_yaml_fragment(root, "chip", detect_chip());
    add_yaml_fragment(root, "board", detect_board());
    add_yaml_fragment(root, "ethernet", detect_ethernet());
    add_yaml_fragment(root, "rom", get_mtd_info());
    add_yaml_fragment(root, "ram", detect_ram());
    add_yaml_fragment(root, "firmware", detect_firmare());
    add_yaml_fragment(root, "sensors", detect_sensors());

    return root;
}

static int backup_with_yaml(const char *backup_file) {
    cJSON *yaml = build_yaml();
    if (!yaml) return EXIT_FAILURE;
    char *string = cYAML_Print(yaml);

    int ret = do_backup(string, strlen(string), backup_file);
    
    free(string);
    cJSON_Delete(yaml);
}

int main(int argc, char *argv[]) {
    // Don't use common option parser for these commands
    if (argc > 1) {
        if (!strcmp(argv[1], "gpio"))
            return gpio_cmd(argc - 1, argv + 1);
        else if (!strcmp(argv[1], "reginfo"))
            return reginfo_cmd(argc - 1, argv + 1);
        else if (!strcmp(argv[1], "watchdog"))
            return watchdog_cmd(argc - 1, argv + 1);
        else if (!strncmp(argv[1], "i2c", 3) || !strncmp(argv[1], "spi", 3))
            return i2cspi_cmd(argc - 1, argv + 1);
        else if (!strcmp(argv[1], "restore") || !strcmp(argv[1], "upgrade"))
            return upgrade_restore_cmd(argc - 1, argv + 1);
        else if (!strcmp(argv[1], "printenv"))
            return cmd_printenv();
        else if (!strcmp(argv[1], "setenv"))
            return cmd_set_env(argc - 1, argv + 1);
        else if (!strcmp(argv[optind], "dmesg"))
            return dmesg();
        else if (!strcmp(argv[optind], "mtd-unlock"))
            return mtd_unlock_cmd();
        else if (!strcmp(argv[optind], "sensor"))
            return snstool_cmd(argc - 1, argv + 1);
#ifdef __arm__
        else if (!strcmp(argv[1], "trace"))
            return ptrace_cmd(argc - 1, argv + 1);
#endif
    }

    const struct option long_options[] = {
        {"chip-name", no_argument, NULL, 'c'},
        {"help", no_argument, NULL, 'h'},
        {"sensor-name", no_argument, NULL, 's'},
        {"temp", no_argument, NULL, 't'},
        {NULL, 0, NULL, 0}};

    int res;
    int option_index;

    while ((res = getopt_long_only(argc, argv, "chst", long_options,
                                   &option_index)) != -1) {
        switch (res) {
        case 'h':
            print_usage();
            return EXIT_SUCCESS;

        case '1':
        case 'c': {
            const char *chipname = getchipname();
            if (!chipname)
                return EXIT_FAILURE;
            puts(chipname);
            return EXIT_SUCCESS;
        }

        case '2':
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

        case '0':

        default:
            printf("found unknown option\n");
            // fall through
        case '?':
            print_usage();
            return EXIT_FAILURE;
        }
    }

    if (argc > optind) {
        if (!strcmp(argv[optind], "backup")) {
            if (argv[optind + 1] == NULL) {
                print_usage();
                return EXIT_FAILURE;
            }
            return backup_with_yaml(argv[optind + 1]);

        } else if (!strcmp(argv[optind], "upload")) {
            return backup_with_yaml(NULL);

        } else {
            printf("found unknown command: %s\n\n", argv[optind]);
            print_usage();
            return EXIT_FAILURE;
        }
    }

    cJSON *yaml = build_yaml();
    if (!yaml) return EXIT_FAILURE;
    char *string = cYAML_Print(yaml);
    printf("%s", string);
    free(string);
    cJSON_Delete(yaml);

    return EXIT_SUCCESS;
}
