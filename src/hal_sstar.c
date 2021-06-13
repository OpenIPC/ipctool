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

sensor_addr_t sstar_possible_i2c_addrs[] = {{SENSOR_SONY, sony_addrs},
                                            {SENSOR_SMARTSENS, ssens_addrs},
                                            {SENSOR_ONSEMI, onsemi_addrs},
                                            {SENSOR_OMNIVISION, omni_addrs},
                                            {0, NULL}};

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
                           unsigned char *reg_addr, unsigned char red_width) {
    unsigned int data_size = 0;

    data_size = red_width * sizeof(unsigned char);
    return 0;
}

int sstar_sensor_write_register(int fd, unsigned char i2c_addr,
                                unsigned int reg_addr, unsigned int reg_width,
                                unsigned int data, unsigned int data_width) {
    char buf[2];

    if (reg_width == 2) {
        buf[0] = (reg_addr >> 8) & 0xff;
        buf[1] = reg_addr & 0xff;
    } else {
        buf[0] = reg_addr & 0xff;
    }

    if (write(fd, buf, data_width) != data_width) {
        return -1;
    }
    return 0;
}

int sstar_sensor_read_register(int fd, unsigned char i2c_addr,
                               unsigned int reg_addr, unsigned int reg_width,
                               unsigned int data_width) {
    char recvbuf[4];
    unsigned int data;

    if (reg_width == 2) {
        recvbuf[0] = (reg_addr >> 8) & 0xff;
        recvbuf[1] = reg_addr & 0xff;
    } else {
        recvbuf[0] = reg_addr & 0xff;
    }

    int data_size = reg_width * sizeof(unsigned char);
    if (write(fd, recvbuf, data_size) != data_size) {
        return -1;
    }

    data_size = data_width * sizeof(unsigned char);
    if (read(fd, recvbuf, data_size) != data_size) {
        return -1;
    }

    if (data_width == 2) {
        data = recvbuf[0] | (recvbuf[1] << 8);
    } else
        data = recvbuf[0];

    return data;
}

static void sstar_hal_cleanup() {}

float sstar_get_temp()
{
    float ret = -237.0;
    char buf[16];
    if(get_regex_line_from_file("/sys/class/mstar/msys/TEMP_R", "Temperature\\s+(.+)", buf, sizeof(buf)))
    {
        ret = strtof(buf, NULL);
    }
    return ret;
}

void setup_hal_sstar() {
    open_sensor_fd = sstar_open_sensor_fd;
    close_sensor_fd = sstar_close_sensor_fd;
    sensor_i2c_change_addr = sstar_sensor_i2c_change_addr;
    sensor_read_register = sstar_sensor_read_register;
    sensor_write_register = sstar_sensor_write_register;
    possible_i2c_addrs = sstar_possible_i2c_addrs;
    hal_cleanup = sstar_hal_cleanup;
    if(!access("/sys/class/mstar/msys/TEMP_R", R_OK))
        hal_temperature = sstar_get_temp;
}
