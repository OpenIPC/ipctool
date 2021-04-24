#include "hal_sstar.h"

#include <stdlib.h>
#include <string.h>

#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "chipid.h"
#include "hal_common.h"
#include "tools.h"

static unsigned char sony_addrs[] = {0x34, 0};
static unsigned char ssens_addrs[] = {0x60, 0};
static unsigned char omni_addrs[] = {0x6c, 0};
static unsigned char onsemi_addrs[] = {0x20, 0};

sensor_addr_t sstar_possible_i2c_addrs[] = {
    {SENSOR_SONY, sony_addrs}, {SENSOR_SMARTSENS, ssens_addrs}, {0, NULL}};

bool sstar_detect_cpu() {
    uint32_t val;
    if (mem_reg(0x1f003c00, &val, OP_READ)) {
        snprintf(chip_id, sizeof(chip_id), "id %#x", val);
        chip_generation = val;
        return true;
    }
    return false;
}

static unsigned long sstar_media_mem() {
    char buf[256];

    if (!get_regex_line_from_file("/proc/cmdline",
                                  "mma_heap=.+sz=(0x[0-9A-Fa-f]+)", buf,
                                  sizeof(buf)))
        return 0;
    return strtoul(buf, NULL, 16) / 1024;
}

unsigned long sstar_totalmem(unsigned long *media_mem) {
    *media_mem = sstar_media_mem();
    return *media_mem + kernel_mem();
}

int sstar_open_sensor_fd() { return common_open_sensor_fd("/dev/i2c-1"); }

void sstar_close_sensor_fd(int fd) { close(fd); }

// Set I2C slave address
int sstar_sensor_i2c_change_addr(int fd, unsigned char addr) {
    if (ioctl(fd, I2C_SLAVE_FORCE, addr >> 1) < 0) {
        return -1;
    }
    return 0;
}

static int sstar_i2c_write(int fd, unsigned char slave_addr,
                           unsigned char *reg_addr, unsigned char reg_cnt) {
    unsigned int data_size = 0;

    data_size = reg_cnt * sizeof(unsigned char);
    if (write(fd, reg_addr, data_size) != data_size) {
        return -1;
    }
    return 0;
}

int sstar_sensor_write_register(int fd, unsigned char i2c_addr,
                                unsigned int reg_addr, unsigned int reg_width,
                                unsigned int data, unsigned int data_width) {}

static int sstar_i2c_read(int fd, unsigned char slave_addr,
                          unsigned char *reg_addr, unsigned char reg_cnt,
                          unsigned char *value, unsigned char value_cnt) {
    unsigned int data_size = 0;

    data_size = reg_cnt * sizeof(unsigned char);
    if (write(fd, reg_addr, data_size) != data_size) {
        return -1;
    }
    data_size = value_cnt * sizeof(unsigned char);
    int n;
    if ((n = read(fd, value, data_size)) != data_size) {
        return -1;
    }

    return 0;
}

int sstar_sensor_read_register(int fd, unsigned char i2c_addr,
                               unsigned int reg_addr, unsigned int reg_width,
                               unsigned int data_width) {
    uint16_t raddr = ntohs(reg_addr);
    uint8_t val = 0;
    sstar_i2c_read(fd, i2c_addr, (unsigned char *)&raddr, reg_width,
                   (unsigned char *)&val, data_width);
    return val;
}

static void sstar_hal_cleanup() {}

void setup_hal_sstar() {
    open_sensor_fd = sstar_open_sensor_fd;
    close_sensor_fd = sstar_close_sensor_fd;
    sensor_i2c_change_addr = sstar_sensor_i2c_change_addr;
    sensor_read_register = sstar_sensor_read_register;
    sensor_write_register = sstar_sensor_write_register;
    possible_i2c_addrs = sstar_possible_i2c_addrs;
    hal_cleanup = sstar_hal_cleanup;
}
