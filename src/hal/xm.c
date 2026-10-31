#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/ioctl.h>
#include <unistd.h>

#include "chipid.h"
#include "hal/common.h"
#include "hal/xm.h"
#include "ram.h"
#include "tools.h"

static unsigned char sony_addrs[] = {0x34, 0};
static unsigned char soi_addrs[] = {0x60, 0x80, 0};
static unsigned char onsemi_addrs[] = {0x20, 0};
static unsigned char ssens_addrs[] = {0x60, 0};
static unsigned char omni_addrs[] = {0x6c, 0};
// ImageDesign MIS2003/MIS2006, which libsns_X123_XM530 probes at 0x30
static unsigned char imagedesign_addrs[] = {0x60, 0};
// only for reference, used in XM510
static unsigned char bg_addrs[] = {0x64, 0};

static sensor_addr_t xm_possible_i2c_addrs[] = {
    {SENSOR_SONY, sony_addrs},       {SENSOR_SMARTSENS, ssens_addrs},
    {SENSOR_ONSEMI, onsemi_addrs},   {SENSOR_SOI, soi_addrs},
    {SENSOR_OMNIVISION, omni_addrs}, {SENSOR_IMAGEDESIGN, imagedesign_addrs},
    {SENSOR_BRIGATES, bg_addrs},     {0, NULL}};

int xm_open_sensor_fd() { return universal_open_sensor_fd("/dev/xm_i2c"); }

/* The V200 series (XM530V200, XM533, XM535D1/D2/W1): the same i2c.ko ioctls,
 * its buses /dev/i2c0..2 (/dev/xm_i2c, xm_i2c1, xm_i2c2 on the XM533). Bus 0
 * carries a sensor on every board seen, and the addresses are the ones those
 * chips' isp daemons probe: SmartSens at 0x30 and 0x32, and SC202CS at 0x10;
 * SOI at 0x40, 0x42 and 0x44; MIS at 0x30 and 0x31; GalaxyCore at 0x37;
 * OV2735 (the SuperPix probe's) at 0x3c. */
static unsigned char v200_ssens_addrs[] = {0x60, 0x64, 0x20, 0};
static unsigned char v200_soi_addrs[] = {0x80, 0x84, 0x88, 0};
static unsigned char v200_sp_addrs[] = {0x78, 0};
static unsigned char v200_imagedesign_addrs[] = {0x60, 0x62, 0};
static unsigned char v200_gc_addrs[] = {0x6e, 0};

static sensor_addr_t xm_v200_possible_i2c_addrs[] = {
    {SENSOR_SONY, sony_addrs},
    {SENSOR_SMARTSENS, v200_ssens_addrs},
    {SENSOR_SOI, v200_soi_addrs},
    {SENSOR_SUPERPIX, v200_sp_addrs},
    {SENSOR_IMAGEDESIGN, v200_imagedesign_addrs},
    {SENSOR_GALAXYCORE, v200_gc_addrs},
    {0, NULL}};

/* Bus n: /dev/i2cN, or the XM533's /dev/xm_i2c (bus 0) and /dev/xm_i2cN. */
static int xm_v200_open_sensor_fd(int adapter_nr) {
    char name[16];

    if (adapter_nr < 0 || adapter_nr > 2)
        return -1;
    snprintf(name, sizeof(name), "/dev/i2c%d", adapter_nr);
    if (!access(name, F_OK)) {
        int fd = universal_open_sensor_fd(name);
        if (fd >= 0)
            return fd;
    }
    if (adapter_nr)
        snprintf(name, sizeof(name), "/dev/xm_i2c%d", adapter_nr);
    else
        snprintf(name, sizeof(name), "/dev/xm_i2c");
    return universal_open_sensor_fd(name);
}

/* The names a V200 kernel gives its machine: OpenIPC's xm530v200, xm533,
 * xm535d1, xm535d2 and xm535w1, the vendor's xm530v200 and xm535 (the
 * XM535D1, D2 and W1 alike) and qiandaohu, its XM533. */
static bool xm_is_v200(const char *name) {
    return !strncmp(name, "XM530V200", 9) || !strncmp(name, "XM533", 5) ||
           !strncmp(name, "XM535", 5);
}

int xm_sensor_read_register(int fd, unsigned char i2c_addr,
                            unsigned int reg_addr, unsigned int reg_width,
                            unsigned int data_width) {
    int ret;
    I2C_DATA_S i2c_data;

    i2c_data.dev_addr = i2c_addr >> 1;
    i2c_data.reg_addr = reg_addr;
    i2c_data.addr_byte_num = reg_width;
    i2c_data.data_byte_num = data_width;

    ret = ioctl(fd, CMD_I2C_READ, &i2c_data);
    if (ret) {
        /* a probe expects most addresses not to answer, and stdout is the
         * report */
#ifndef NDEBUG
        printf("xm_i2c read failed!\n");
#endif
        return -1;
    }

    return i2c_data.data;
}

int xm_sensor_write_register(int fd, unsigned char i2c_addr,
                             unsigned int reg_addr, unsigned int reg_width,
                             unsigned int data, unsigned int data_width) {
    int ret;
    I2C_DATA_S i2c_data;

    i2c_data.dev_addr = i2c_addr >> 1;
    i2c_data.reg_addr = reg_addr;
    i2c_data.addr_byte_num = reg_width;
    i2c_data.data = data;
    i2c_data.data_byte_num = data_width;

    ret = ioctl(fd, CMD_I2C_WRITE, &i2c_data);

    if (ret) {
#ifndef NDEBUG
        printf("xm_i2c write failed!\n");
#endif
        return -1;
    }

    return 0;
}

static void xm_hal_cleanup() { restore_printk(); }

void setup_hal_xm() {
    open_i2c_sensor_fd = xm_open_sensor_fd;
    i2c_change_addr = dummy_sensor_i2c_change_addr;
    i2c_read_register = xm_sensor_read_register;
    i2c_write_register = xm_sensor_write_register;
    possible_i2c_addrs = xm_possible_i2c_addrs;
    if (xm_is_v200(chip_name)) {
        open_i2c_sensor_fd = xm_v200_open_sensor_fd;
        possible_i2c_addrs = xm_v200_possible_i2c_addrs;
    }
    hal_cleanup = xm_hal_cleanup;
#ifndef STANDALONE_LIBRARY
    hal_totalmem = xm_totalmem;
#endif
}

static unsigned long xm_media_mem() {
    FILE *f = fopen("/proc/umap/mmz", "r");
    if (!f)
        return 0;

    unsigned long mmem = 0;
    regex_t regex;
    regmatch_t matches[3];
    if (!regex_compile(&regex, "\\[(0x[0-9a-f]+)-+(0x[0-9a-f]+)\\]"))
        goto exit;

    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    while ((read = getline(&line, &len, f)) != -1) {
        if (regexec(&regex, line, sizeof(matches) / sizeof(matches[0]),
                    (regmatch_t *)&matches, 0) == 0) {
            regoff_t start = matches[1].rm_so;
            regoff_t end = matches[1].rm_eo;
            line[end] = 0;
            unsigned long memstart = strtoul(line + start, NULL, 16);

            start = matches[2].rm_so;
            end = matches[2].rm_eo;
            line[end] = 0;
            unsigned long memend = strtoul(line + start, NULL, 16);

            mmem = (memend - memstart) / 1024;
            break;
        }
    }
    if (line)
        free(line);

exit:
    regfree(&regex);
    fclose(f);
    return mmem;
}

unsigned long xm_totalmem(unsigned long *media_mem) {
    *media_mem = xm_media_mem();
    return *media_mem + kernel_mem();
}

bool xm_detect_cpu(char *chip_name, uint32_t base) {
    char buf[256];
    (void)base;

    bool res =
        line_from_file("/proc/cpuinfo", "^Hardware.+(xm.+)", buf, sizeof(buf));
    if (!res) {
        // the vendor's XM533 kernel calls its machine qiandaohu
        if (!line_from_file("/proc/cpuinfo", "^Hardware.+(qiandaohu)", buf,
                            sizeof(buf)))
            return false;
        strcpy(buf, "xm533");
    }
    strcpy(chip_name, buf);
    char *ptr = chip_name;
    while (*ptr) {
        *ptr = toupper(*ptr);
        ptr++;
    }
    /* Only the two layouts hal/xm_padmux.c knows. Any other name keeps
     * generation 0, and pad mux answers that it has no table rather than
     * reading registers on a layout nobody checked. */
    if (!strcmp(chip_name, "XM510"))
        chip_generation = CHIP_XM510;
    if (!strcmp(chip_name, "XM530")) {
        chip_generation = CHIP_XM530; /* XM550 is the same die */
        uint32_t reg;
        if (mem_reg(0x100B001C, (uint32_t *)&reg, OP_READ)) {
            // bank_size, 0x2 for 512M and 0x3 for 1024M
            if (reg == 3)
                strcpy(chip_name, "XM550");
        }
    }

    return true;
}
