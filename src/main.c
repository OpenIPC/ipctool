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
#include "hal_hisi.h"
#include "hwinfo.h"
#include "mtd.h"
#include "network.h"
#include "ram.h"
#include "sensors.h"
#include "tools.h"
#include "uboot.h"
#include "version.h"

#include "vendors/buildroot.h"
#include "vendors/hankvision.h"
#include "vendors/openipc.h"
#include "vendors/openwrt.h"
#include "vendors/ruision.h"
#include "vendors/xm.h"

#define RESET_CL "\x1b[0m"
#define FG_RED "\x1b[31m"

void Help() {
#ifndef SKIP_VERSION
    printf("ipctool, version: ");
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
           "\t--sensor_id\n"
           "\t--temp\n"
           "\n"
           "\t--dmesg\n"
           "\t--printenv\n"
           "\t--setenv key=value\n"
           "\n"
           "\t--backup <filename>\n"
           "\t[--skip-env] [--force] --restore [mac]\n"
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
                chip_manufacturer, chip_id);
    if (chip_generation == 0x3516E300) {
        char buf[1024];
        if (hisi_ev300_get_die_id(buf, sizeof buf)) {
            yaml_printf("  id: %s\n", buf);
        }
    }
}

bool get_board_id() {
    if (is_xm_board()) {
        gather_xm_board_info();
        return true;
    } else if (is_openwrt_board()) {
        gather_openwrt_board_info();
        return true;
    } else if (is_br_board()) {
        gather_br_board_info();
        return true;
    } else if (is_ruision_board()) {
        gather_ruision_board_info();
        return true;
    } else if (is_hankvision_board()) {
        gather_hankvision_board_info();
        return true;
    }
    return false;
}

#define MAX_YAML 1024 * 64
bool wait_mode = false;
const char *backup_file;
static bool backup_mode() {
    // prevent double backup creation and don't backup OpenWrt firmware
    if (!udp_lock() || is_openipc_board())
        return false;

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

int main(int argc, char *argv[]) {
    const char *short_options = "";
    const struct option long_options[] = {
        {"help", no_argument, NULL, 'h'},
        {"chip_id", no_argument, NULL, 'c'},
        {"sensor_id", no_argument, NULL, 's'},
        {"temp", no_argument, NULL, 't'},
        {"printenv", no_argument, NULL, 'p'},
        {"setenv", required_argument, NULL, 'e'},
        {"dmesg", no_argument, NULL, 'd'},
        {"wait", no_argument, NULL, 'w'},
        {"backup", required_argument, NULL, 'b'},
        {"restore", optional_argument, NULL, 'r'},
        {"skip-env", no_argument, NULL, '0'},
        {"force", no_argument, NULL, 'f'},
        {"upgrade", optional_argument, NULL, 'u'},
        {NULL, 0, NULL, 0}};

    int rez;
    int option_index;
    bool skip_env = false;
    bool force = false;

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

        case 'p':
            printenv();
            return 0;

        case 'e':
            cmd_set_env(optarg);
            return 0;

        case 'd':
            dmesg();
            return 0;

        case 'w':
            wait_mode = true;
            break;

        case '0':
            skip_env = true;
            break;

        case 'f':
            force = true;
            break;

        case 'b':
            backup_file = optarg;
            wait_mode = true;
            break;

        case 'r':
            return restore_backup(optarg, skip_env, force);

        case 'u':
            return do_upgrade(optarg, force);

        default:
            printf("found unknown option\n");
        case '?':
            Help();
            return EXIT_FAILURE;
        }
    }

    if (backup_mode())
        // child process
        return EXIT_SUCCESS;

    if (getchipid()) {
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
        printf("state: %sStart\n", backup_file ? "save" : "upload");
        fclose(backup_fp);
        int status;
        wait(&status);
        printf("state: %sEnd, %d\n", backup_file ? "save" : "upload",
               WEXITSTATUS(status));
    }

    return EXIT_SUCCESS;
}
