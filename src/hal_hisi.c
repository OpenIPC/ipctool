#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <linux/ioctl.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <unistd.h>

#include "hal_common.h"

static unsigned char sony_addrs[] = {0x34, NULL};
static unsigned char soi_addrs[] = {0x80, NULL};
static unsigned char onsemi_addrs[] = {0x20, NULL};
static unsigned char ssens_addrs[] = {0x60, NULL};

sensor_addr_t hisi_possible_i2c_addrs[] = {{SENSOR_SONY, sony_addrs},
                                           {SENSOR_SOI, soi_addrs},
                                           {SENSOR_ONSEMI, onsemi_addrs},
                                           {SENSOR_SMARTSENS, ssens_addrs},
                                           {0, NULL}};

int hisi_open_sensor_fd() {
    int adapter_nr = 0; /* probably dynamically determined */
    char filename[FILENAME_MAX];

    snprintf(filename, sizeof(filename), "/dev/i2c-%d", adapter_nr);

    return common_open_sensor_fd(filename);
}

// Set I2C slave address
int hisi_sensor_i2c_change_addr(int fd, unsigned char addr) {
    int ret = ioctl(fd, I2C_SLAVE_FORCE, (addr >> 1));
    if (ret < 0) {
        fprintf(stderr, "CMD_SET_DEV error!\n");
        return ret;
    }
    return ret;
}

int hisi_sensor_write_register(int fd, unsigned char i2c_addr,
                               unsigned int reg_addr, unsigned int reg_width,
                               unsigned int data, unsigned int data_width) {
    int idx = 0;
    int ret;
    char buf[8];

    if (reg_addr == 2) {
        buf[idx] = (reg_addr >> 8) & 0xff;
        idx++;
        buf[idx] = reg_addr & 0xff;
        idx++;
    } else {
        buf[idx] = reg_addr & 0xff;
        idx++;
    }

    if (data == 2) {
        buf[idx] = (data >> 8) & 0xff;
        idx++;
        buf[idx] = data & 0xff;
        idx++;
    } else {
        buf[idx] = data & 0xff;
        idx++;
    }

    ret = write(fd, buf, (reg_addr + data));
    if (ret < 0) {
        printf("I2C_WRITE error!\n");
        return -1;
    }
    return 0;
}

int hisi_sensor_read_register(int fd, unsigned char i2c_addr,
                              unsigned int reg_addr, unsigned int reg_width,
                              unsigned int data_width) {
    static struct i2c_rdwr_ioctl_data rdwr;
    static struct i2c_msg msg[2];
    unsigned int reg_addr_end = reg_addr;
    unsigned char buf[4];
    unsigned int data;

    // measure ioctl execution time to exit early in too slow response
    struct rusage start_time;
    int ret = getrusage(RUSAGE_SELF, &start_time);

    memset(buf, 0x0, sizeof(buf));

    msg[0].addr = i2c_addr >> 1;
    msg[0].flags = 0;
    msg[0].len = reg_width;
    msg[0].buf = buf;

    msg[1].addr = i2c_addr >> 1;
    msg[1].flags = 0;
    msg[1].flags |= I2C_M_RD;
    msg[1].len = data_width;
    msg[1].buf = buf;

    rdwr.msgs = &msg[0];
    rdwr.nmsgs = (__u32)2;

    for (int cur_addr = reg_addr; cur_addr <= reg_addr_end; cur_addr += 1) {
        if (reg_width == 2) {
            buf[0] = (cur_addr >> 8) & 0xff;
            buf[1] = cur_addr & 0xff;
        } else
            buf[0] = cur_addr & 0xff;

        int retval = ioctl(fd, I2C_RDWR, &rdwr);
        struct rusage end_time;
        int ret = getrusage(RUSAGE_SELF, &end_time);
        if (end_time.ru_stime.tv_sec - start_time.ru_stime.tv_sec > 2) {
            fprintf(stderr, "Buggy I2C driver detected! Load all ko modules\n");
            exit(2);
        }
        start_time = end_time;

        if (retval != 2) {
            return -1;
        }

        if (data_width == 2) {
            data = buf[1] | (buf[0] << 8);
        } else
            data = buf[0];
    }

    return data;
}

void setup_hal_hisi() {
    open_sensor_fd = hisi_open_sensor_fd;
    sensor_i2c_change_addr = hisi_sensor_i2c_change_addr;
    sensor_read_register = hisi_sensor_read_register;
    sensor_write_register = hisi_sensor_write_register;
    possible_i2c_addrs = hisi_possible_i2c_addrs;
}
