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
#include "snstool.h"
#include "tools.h"
#include "uboot.h"
#include "version.h"
#include "watchdog.h"

#include "vendors/buildroot.h"
#include "vendors/common.h"

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

void yaml_fragment(cJSON *json) {
    if (!json)
        return;

    cJSON *root;
    int cnt = 0;
    cJSON_ArrayForEach(root, json) {
        cJSON *sub;
        cJSON_ArrayForEach(sub, root) cnt++;
    }

    // Don't show empty section
    if (cnt == 0)
        goto bailout;

    char *string = cYAML_Print(json);
    fprintf(stdout, "%s", string);
    if (backup_fp)
        fprintf(backup_fp, "%s", string);
    free(string);

bailout:
    cJSON_Delete(json);
}

enum {
    BACKUP_WAIT = 1 << 0,
};

#define MAX_YAML (1024 * 64)
static bool backup_with_yaml(const char *backup_file, unsigned modes) {
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
        int ret = do_backup(yaml, yaml_sz, modes & BACKUP_WAIT, backup_file);
        free(yaml);

        if (ret)
            exit(ret);
        return true;
    }
}

static bool auto_backup(unsigned modes) {
    // prevent double backup creation and don't back up OpenWrt firmware
    if (!udp_lock() || is_openipc_board())
        return false;

    return backup_with_yaml(NULL, modes);
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
        {"wait", no_argument, NULL, 'w'},
        {NULL, 0, NULL, 0}};

    int res;
    int option_index;
    unsigned modes = 0;

    while ((res = getopt_long_only(argc, argv, "chstw", long_options,
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

        case 'w':
            modes |= BACKUP_WAIT;
            break;

        case '0':

        default:
            printf("found unknown option\n");
        case '?':
            print_usage();
            return EXIT_FAILURE;
        }
    }

    const char *backupAction = "upload";
    if (argc > optind) {
        if (!strcmp(argv[optind], "backup")) {
            if (argv[optind + 1] == NULL) {
                print_usage();
                return EXIT_FAILURE;
            }

            modes |= BACKUP_WAIT;
            backupAction = "save";
            if (backup_with_yaml(argv[optind + 1], modes)) {
                // child process
                return EXIT_SUCCESS;
            }
            goto start_yaml;
        } else {
            printf("found unknown command: %s\n\n", argv[optind]);
            print_usage();
            return EXIT_FAILURE;
        }
    }

    if (auto_backup(modes))
        // child process
        return EXIT_SUCCESS;

start_yaml:
    if (getchipname()) {
        yaml_printf("---\n");
        yaml_fragment(detect_board());
        yaml_fragment(detect_chip());
        yaml_fragment(detect_ethernet());
        print_mtd_info();
        yaml_fragment(detect_ram());
        yaml_fragment(detect_firmare());
    } else
        return EXIT_FAILURE;

    // flush stdout before go to sensor detection to avoid buggy kernel
    // freezes
    tcdrain(STDOUT_FILENO);
    yaml_fragment(detect_sensors());

    if (modes & BACKUP_WAIT && backup_fp) {
        // trigger child process
        printf("---\n");
        printf("state: %sStart\n", backupAction);
        fclose(backup_fp);
        int status;
        wait(&status);
        printf("state: %sEnd, %d\n", backupAction, WEXITSTATUS(status));
    }

    return EXIT_SUCCESS;
}
