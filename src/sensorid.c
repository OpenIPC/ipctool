#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <linux/ioctl.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/resource.h>

#include "sensorid.h"

char sensor_id[128];
char sensor_manufacturer[128];

#define I2C_ADAPTER "/dev/i2c-0"

// Set I2C slave address
int sensor_i2c_change_addr(int fd, unsigned char addr) {
    int ret = ioctl(fd, I2C_SLAVE_FORCE, (addr >> 1));
    if (ret < 0) {
        printf("CMD_SET_DEV error!\n");
        return ret;
    }
    return ret;
}

int sensor_i2c_init() {
    int ret, fd;

    fd = open(I2C_ADAPTER, O_RDWR);
    if (fd < 0) {
        printf("Open " I2C_ADAPTER " error!\n");
        return -1;
    }

    return fd;
}

int sensor_i2c_exit(int fd) {
    if (fd >= 0) {
        close(fd);
        return true;
    }
    return false;
}

int sensor_write_register(int fd, int addr, int data) {
    int idx = 0;
    int ret;
    char buf[8];

    if (addr == 2) {
        buf[idx] = (addr >> 8) & 0xff;
        idx++;
        buf[idx] = addr & 0xff;
        idx++;
    } else {
        buf[idx] = addr & 0xff;
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

    ret = write(fd, buf, (addr + data));
    if (ret < 0) {
        printf("I2C_WRITE error!\n");
        return -1;
    }
    return 0;
}

int sensor_read_register(int fd, unsigned char i2c_addr, unsigned int reg_addr,
                         unsigned int reg_width, unsigned int data_width) {
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

int detect_sony_sensor(int fd) {
    const unsigned char i2c_addr = 0x34;
    if (sensor_i2c_change_addr(fd, i2c_addr) < 0)
        return false;

    // from IMX335 datasheet, p.40
    // 316Ah - 2-6 bits are 1, 7 bit is 0
    int ret316a = sensor_read_register(fd, i2c_addr, 0x316A, 2, 1);
    // early break
    if (ret316a == -1)
        return false;

    if (ret316a > 0 && ((ret316a & 0xfc) == 0x7c)) {
        sprintf(sensor_id, "IMX335");
        return true;
    }

    int ret3013 = sensor_read_register(fd, i2c_addr, 0x3013, 2, 1);
    if (ret3013 == 64) {
        sprintf(sensor_id, "IMX323");
        return true;
    }

    int ret31dc = sensor_read_register(fd, i2c_addr, 0x31DC, 2, 1);
    if (ret31dc > 0) {
        switch (ret31dc & 6) {
        case 4:
            sprintf(sensor_id, "IMX307");
            break;
        case 6:
            sprintf(sensor_id, "IMX327");
            break;
        default:
            sprintf(sensor_id, "IMX29%d", ret31dc & 7);
            return true;
        }
        return true;
    }

    return false;
}

// tested on F22, F23, F37
int detect_soi_sensor(int fd) {
    const unsigned char i2c_addr = 0x80;
    if (sensor_i2c_change_addr(fd, i2c_addr) < 0)
        return false;

    // Product ID number (Read only)
    int pid = sensor_read_register(fd, i2c_addr, 0xa, 1, 1);
    // early break
    if (pid == -1)
        return false;

    // Product version number (Read only)
    int ver = sensor_read_register(fd, i2c_addr, 0xb, 1, 1);
    if (pid == 0xf) {
        sprintf(sensor_id, "JX-F%x", ver);
        return true;
    }
    return false;
}

int get_sensor_id() {
    int fd = sensor_i2c_init();

    if (detect_soi_sensor(fd)) {
        strcpy(sensor_manufacturer, "Silicon Optronics");
        return EXIT_SUCCESS;
    } else if (detect_sony_sensor(fd)) {
        strcpy(sensor_manufacturer, "Sony");
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
