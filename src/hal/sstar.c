#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>

#include "chipid.h"
#include "hal/common.h"
#include "hal/sstar.h"
#include "tools.h"

static unsigned char onsemi_addrs[] = {0x20, 0};
static unsigned char sony_addrs[] = {0x34, 0};
static unsigned char ssens_addrs[] = {0x60, 0};
static unsigned char omni_addrs[] = {0x6C, 0};
static unsigned char gc_addrs[] = {0x42, 0x52, 0x6E, 0};

static sensor_addr_t sstar_possible_i2c_addrs[] = {
    {SENSOR_ONSEMI, onsemi_addrs},
    {SENSOR_SONY, sony_addrs},
    {SENSOR_SMARTSENS, ssens_addrs},
    {SENSOR_OMNIVISION, omni_addrs},
    {SENSOR_GALAXYCORE, gc_addrs},
    {0, NULL},
};

bool mstar_detect_cpu(char *chip_name) {
    uint32_t val = 0;

    if (mem_reg(MSTAR_ADDR, &val, OP_READ)) {
        switch (val & 0xF000) {
        case 0x6000:
            strcpy(chip_name, "MSC313E");
            break;
        case 0x7000:
            strcpy(chip_name, "MSC316DC");
            break;
        case 0x8000:
            strcpy(chip_name, "MSC318");
            break;
        }
        return true;
    }

    return false;
}

bool sstar_detect_cpu(char *chip_name) {
    uint32_t val = 0;

    char *soc_env = getenv("SOC");
    if (soc_env && *soc_env) {
        strcpy(chip_name, soc_env);
        return true;
    }

    if (mem_reg(SSTAR_ADDR, &val, OP_READ)) {
        chip_generation = val;
        switch (val) {
        case INFINITY5:
            strcpy(chip_name, "SSC326X/SSC328X/SSC329X");
            break;
        case INFINITY6:
            strcpy(chip_name, "SSC323X/SSC325X/SSC327X");
            break;
        case INFINITY6E:
            strcpy(chip_name, "SSC336X/SSC338X/SSC339X");
            break;
        case INFINITY6B:
            strcpy(chip_name, "SSC333X/SSC335X/SSC337X");
            break;
        }
        return true;
    }

    return false;
}

static bool sstar_get_die_id(char *buf, ssize_t len) {
    uint32_t base = 0, val = 0;

    if (!chip_generation) {
        return false;
    }

    if (chip_generation == INFINITY6E) {
        base = CHIP_ADDR1;
    } else {
        base = CHIP_ADDR2;
    }

    for (uint32_t addr = base + 8; addr >= base; addr -= 4) {
        if (!mem_reg(addr, &val, OP_READ)) {
            return false;
        }
        int outsz = snprintf(buf, len, "%04X", val);
        buf += outsz;
        len -= outsz;
    }

    return true;
}

static bool sstar_detect_brom_tag(uint32_t addr, char *buf) {
    mem_reg(addr, (uint32_t*)buf, OP_READ);

    if (buf[0] == 'M' && buf[1] == 'V' && buf[2] == 'X') {
        for (int i = 1; i < 8; i++) {
            mem_reg(addr + i * 4, (uint32_t*)(buf + i * 4), OP_READ);
        }
        return true;
    }

    return false;
}

static int sstar_open_sensor_fd() {
    return universal_open_sensor_fd("/dev/i2c-1");
}

static void sstar_hal_cleanup() {
    //
}

static float sstar_get_temp() {
    char buf[16];

    if (!line_from_file(TEMP_PATH, "Temperature.(.+)",
                buf, sizeof(buf))) {
        return 0;
    }

    return strtof(buf, NULL);
}

static unsigned long sstar_media_mem() {
    char buf[256];

    if (!line_from_file(CMD_PATH, "mma_heap=.+sz=(\\w+)",
                buf, sizeof(buf))) {
        return 0;
    }

    return strtoul(buf, NULL, 16) / 1024;
}

#ifndef STANDALONE_LIBRARY
static unsigned long sstar_totalmem(unsigned long *media_mem) {
    uint32_t val = 0;

    *media_mem = sstar_media_mem();
    if (mem_reg(MSTAR_ADDR, &val, OP_READ)) {
        return (1 << (val >> 12)) * 1024 - 1;
    }

    return *media_mem + kernel_mem();
}

static void sstar_chip_properties(cJSON *j_inner) {
    char buf[128];

    if (sstar_get_die_id(buf, sizeof buf)) {
        ADD_PARAM("id", buf);
    }

    if (sstar_detect_brom_tag(BROM_ADDR1, buf) ||
            sstar_detect_brom_tag(BROM_ADDR2, buf)) {
        ADD_PARAM("tag", buf);
    }
}
#endif

void sstar_setup_hal() {
    open_i2c_sensor_fd = sstar_open_sensor_fd;
    possible_i2c_addrs = sstar_possible_i2c_addrs;
    hal_cleanup = sstar_hal_cleanup;
    if (!access(TEMP_PATH, R_OK)) {
        hal_temperature = sstar_get_temp;
    }
#ifndef STANDALONE_LIBRARY
    hal_totalmem = sstar_totalmem;
    hal_chip_properties = sstar_chip_properties;
#endif
}
