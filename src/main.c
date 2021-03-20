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

struct REG_MDIO_RWCTRL {
    unsigned int phy_inaddr : 5;
    unsigned int frq_dv : 3;
    unsigned int phy_exaddr : 5;
    unsigned int rw : 1;
    unsigned int res : 1;
    bool finish : 1;
    unsigned int cpu_data_in : 16;
};

#define MDIO_RWCTRL 0x1100
#define MDIO_RO_DATA 0x1104
#define U_MDIO_PHYADDR 0x0108
#define D_MDIO_PHYADDR 0x2108
#define U_MDIO_RO_STAT 0x010C
#define D_MDIO_RO_STAT 0x210C

uint32_t hieth_readl(uint32_t base, uint32_t regaddr) {
    uint32_t val;
    if (mem_reg(base + regaddr, &val, OP_READ)) {
        return val;
    }
    return 0x1111;
}

void hieth_writel(uint32_t val, uint32_t base, uint32_t regaddr) {
    if (!mem_reg(base + regaddr, &val, OP_WRITE)) {
        printf("write error\n");
    }
}

/* hardware set bit'15 of MDIO_REG(0) if mdio ready */
#define test_mdio_ready(base) (hieth_readl(base, MDIO_RWCTRL) & (1 << 15))

static int wait_mdio_ready(uint32_t base) {
    int timeout_us = 1000;
    while (--timeout_us && !test_mdio_ready(base))
        usleep(1);
    return timeout_us;
}

#define MDIO_MK_RWCTL(cpu_data_in, finish, rw, phy_exaddr, frq_div,            \
                      phy_regnum)                                              \
    (((cpu_data_in) << 16) | (((finish)&0x01) << 15) | (((rw)&0x01) << 13) |   \
     (((phy_exaddr)&0x1F) << 8) | (((frq_div)&0x7) << 5) |                     \
     ((phy_regnum)&0x1F))

#define mdio_start_phyread(base, frq_dv, phy_addr, regnum)                     \
    hieth_writel(MDIO_MK_RWCTL(0, 0, 0, phy_addr, frq_dv, regnum), base,       \
                 MDIO_RWCTRL)

#define mdio_get_phyread_val(base) (hieth_readl(base, MDIO_RO_DATA) & 0xFFFF)

int hieth_mdio_read(int frq_dv, int phy_addr, uint32_t base, int regnum) {
    int val = 0;

    if (!wait_mdio_ready(base)) {
        fprintf(stderr, "mdio busy\n");
        goto error_exit;
    }

    mdio_start_phyread(base, frq_dv, phy_addr, regnum);

    if (wait_mdio_ready(base))
        val = mdio_get_phyread_val(base);
    else
        fprintf(stderr, "read timeout\n");

error_exit:
#if 0
    fprintf(stderr, "phy_addr = %d, regnum = %d, val = 0x%04x\n", phy_addr,
            regnum, val);
#endif

    return val;
}

cJSON *detect_ethernet() {
    cJSON *fake_root = cJSON_CreateObject();
    cJSON *j_inner = cJSON_CreateObject();
    cJSON_AddItemToObject(fake_root, "ethernet", j_inner);

    char buf[1024];

    if (get_mac_address(buf, sizeof buf)) {
        ADD_PARAM("mac", buf);
    };

    uint32_t mdio_base = 0;
    switch (chip_generation) {
    case 0x35180100:
    case 0x3518E200:
        mdio_base = 0x10090000;
        break;
    case 0x3516C300:
        mdio_base = 0x10050000;
        break;
    }

    if (mdio_base) {
        struct REG_MDIO_RWCTRL reg;
        if (mem_reg(mdio_base + MDIO_RWCTRL, (uint32_t *)&reg, OP_READ)) {
            uint32_t my_phyaddr = hieth_readl(mdio_base, U_MDIO_PHYADDR);
            ADD_PARAM_FMT("u-mdio-phyaddr", "%d", my_phyaddr);

            unsigned long phy_id;
            unsigned short id1, id2;
            id1 = hieth_mdio_read(reg.frq_dv, my_phyaddr, mdio_base, 0x02);
            id2 = hieth_mdio_read(reg.frq_dv, my_phyaddr, mdio_base, 0x03);
            phy_id = (((id1 & 0xffff) << 16) | (id2 & 0xffff));
            ADD_PARAM_FMT("phy-id", "0x%.8lx", phy_id);
            ADD_PARAM_FMT("d-mdio-phyaddr", "%x",
                          hieth_readl(mdio_base, D_MDIO_PHYADDR));
        }
    }
    return fake_root;
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
            show_yaml(detect_ethernet());
            exit(0);
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
