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
#include "hisi/hal_hisi.h"
#include "hwinfo.h"
#include "i2cspi.h"
#include "mtd.h"
#include "network.h"
#include "ptrace.h"
#include "ram.h"
#include "reginfo.h"
#include "sensors.h"
#include "tools.h"
#include "uboot.h"
#include "version.h"
#include "watchdog.h"

#include "vendors/buildroot.h"
#include "vendors/common.h"

#define RESET_CL "\x1b[0m"
#define FG_RED "\x1b[31m"

void Help() {
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
        "Usage:  ipctool [OPTIONS] [COMMANDS]\n"
        "Where:\n"
        "  -c, --chip-id             read chip id\n"
        "  -s, --sensor-id           read sensor model and control line\n"
        "  -t, --temp                read chip temperature (where supported)\n"
        "\n"
        "  backup <filename>         save backup into a file\n"
        "  restore [mac|filename]    restore from backup (cloud-based or local "
         "file)\n"
        "     [-s, --skip-env]       skip environment\n"
        "     [-f, --force]          enforce\n"
        "  upgrade <bundle>          upgrade to OpenIPC firmware (experimental feature, use only on cameras with UART)\n"
        "     [-f, --force]          enforce\n"
        "  printenv                  drop-in replacement for fw_printenv\n"
        "  setenv <key> <value>      drop-in replacement for fw_setenv\n"
        "  dmesg                     drop-in replacement for dmesg\n"
        "  (i2c|spi)get <device address> <register>\n"
        "                            read data from I2C/SPI device\n"
        "  (i2c|spi)set <device address> <register> <new value>\n"
        "                            write a value to I2C/SPI device\n"
        "  (i2c|spi)dump [--script] <device address> <from register> <to "
        "register>\n"
        "                            dump data from I2C/SPI device\n"
        "  reginfo [--script]        dump current status of pinmux registers\n"
        "  gpio (scan|mux)           GPIO utilities\n"
        "  trace <full/path/to/executable> [arguments]\n"
        "                            dump original firmware calls and data "
        "structures\n"
        "  -h, --help                this help\n");
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
    if (!json)
        return;

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
                chip_manufacturer, chip_name);
    if (chip_generation == 0x3516E300) {
        char buf[1024];
        if (hisi_ev300_get_die_id(buf, sizeof buf)) {
            yaml_printf("  id: %s\n", buf);
        }
    }
}

#define MAX_YAML 1024 * 64
static bool backup_with_yaml(const char *backup_file, bool wait_mode) {
    int fds[2];
    if (pipe(fds) == -1) {
        fprintf(stderr, "Pipe Failed");
        exit(1);
    }

    pid_t child_pid = fork();
    if (child_pid < 0) {
        exit(1);
    } else if (child_pid > 0) {
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
        int ret = do_backup(yaml, yaml_sz, wait_mode, backup_file);
        free(yaml);

        if (ret)
            exit(ret);
        return true;
    }
}

static bool auto_backup(bool wait_mode) {
    // prevent double backup creation and don't backup OpenWrt firmware
    if (!udp_lock() || is_openipc_board())
        return false;

    return backup_with_yaml(NULL, wait_mode);
}

int main(int argc, char *argv[]) {
    // Don't use common option parser for these commands
    if (argc > 1) {
        if (!strcmp(argv[1], "trace"))
            return ptrace_cmd(argc - 1, argv + 1);
        else if (!strcmp(argv[1], "gpio"))
            return gpio_cmd(argc - 1, argv + 1);
        else if (!strcmp(argv[1], "reginfo"))
            return reginfo_cmd(argc - 1, argv + 1);
        else if (!strcmp(argv[1], "watchdog"))
            return watchdog_cmd(argc - 1, argv + 1);
        else if (!strncmp(argv[1], "i2c", 3) || !strncmp(argv[1], "spi", 3))
            return i2cspi_cmd(argc - 1, argv + 1);
        else if (!strcmp(argv[1], "restore" ) || !strcmp(argv[1], "upgrade"))
            return upgrade_restore_cmd(argc - 1, argv + 1);
    }

    const struct option long_options[] = {
        {"chip-id", no_argument, NULL, 'c'},
        {"help", no_argument, NULL, 'h'},
        {"sensor-id", no_argument, NULL, 's'},
        {"temp", no_argument, NULL, 't'},
        {"wait", no_argument, NULL, 'w'},

        // Keep for compability reasons
        {"chip_id", no_argument, NULL, '1'},
        {"sensor_id", no_argument, NULL, '2'},

        {NULL, 0, NULL, 0}};

    int res;
    int option_index;
    bool wait_mode = false;

    while ((res = getopt_long_only(argc, argv, "cs", long_options,
                                   &option_index)) != -1) {
        switch (res) {
        case 'h':
            Help();
            return 0;

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

        case 'w':
            wait_mode = true;
            break;

        case '0':

        default:
            printf("found unknown option\n");
        case '?':
            Help();
            return EXIT_FAILURE;
        }
    }

    if (argc > optind) {
        if (!strcmp(argv[optind], "dmesg")) {
            return dmesg();
        } else if (!strcmp(argv[optind], "backup")) {
            if (argv[optind + 1] == NULL) {
                Help();
                return EXIT_FAILURE;
            }

            if (backup_with_yaml(argv[optind + 1], wait_mode)) {
                // child process
                return EXIT_SUCCESS;
            }
            goto start_yaml;
        } else if (!strcmp(argv[optind], "printenv")) {
            return cmd_printenv();
        } else if (!strcmp(argv[optind], "setenv")) {
            return cmd_set_env(argc - optind, argv + optind);
        } else {
            printf("found unknown command: %s\n\n", argv[optind]);
            Help();
            return EXIT_FAILURE;
        }
    }

    if (auto_backup(wait_mode))
        // child process
        return EXIT_SUCCESS;

start_yaml:
    if (getchipname()) {
        yaml_printf("---\n");
        if (get_board_id()) {
            print_board_id();
        }
        print_system_id();
        print_chip_id();
        show_yaml(detect_ethernet());
        print_mtd_info();
        show_yaml(detect_ram());
        show_yaml(detect_firmare());
    } else
        return EXIT_FAILURE;

    // flush stdout before go to sensor detection to avoid buggy kernel
    // freezes
    tcdrain(STDOUT_FILENO);
    show_yaml(detect_sensors());

    if (wait_mode && backup_fp) {
        // trigger child process
        printf("---\n");
        printf("state: %sStart\n", "upload");
        fclose(backup_fp);
        int status;
        wait(&status);
        printf("state: %sEnd, %d\n", "upload", WEXITSTATUS(status));
    }

    return EXIT_SUCCESS;
}
