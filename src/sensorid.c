#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <linux/ioctl.h>
#include <sys/ioctl.h>

#include "sensorid.h"

char sensor_id[128];
char sensor_manufacturer[128];

#define I2C_ADAPTER "/dev/i2c-0"
static int g_fd = -1;
const unsigned char sensor_i2c_addr = 0x34; /* I2C Address of IMX290 */
const unsigned int sensor_addr_byte = 2;
const unsigned int sensor_data_byte = 1;

// Set I2C slave address
int sensor_i2c_change_addr(int addr) {
    int ret = ioctl(g_fd, I2C_SLAVE_FORCE, (sensor_i2c_addr >> 1));
    if (ret < 0) {
        printf("CMD_SET_DEV error!\n");
        return ret;
    }
    return ret;
}

int sensor_i2c_init() {
    int ret;

    if (g_fd >= 0) {
        return 0;
    }

    g_fd = open(I2C_ADAPTER, O_RDWR);
    if (g_fd < 0) {
        printf("Open " I2C_ADAPTER " error!\n");
        return -1;
    }

    return 0;
}

int sensor_i2c_exit(void) {
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
        return 0;
    }
    return -1;
}

int sensor_write_register(int addr, int data) {
    int idx = 0;
    int ret;
    char buf[8];

    if (sensor_addr_byte == 2) {
        buf[idx] = (addr >> 8) & 0xff;
        idx++;
        buf[idx] = addr & 0xff;
        idx++;
    } else {
        buf[idx] = addr & 0xff;
        idx++;
    }

    if (sensor_data_byte == 2) {
        buf[idx] = (data >> 8) & 0xff;
        idx++;
        buf[idx] = data & 0xff;
        idx++;
    } else {
        buf[idx] = data & 0xff;
        idx++;
    }

    ret = write(g_fd, buf, (sensor_addr_byte + sensor_data_byte));
    if (ret < 0) {
        printf("I2C_WRITE error!\n");
        return -1;
    }
    return 0;
}

int sensor_read_register(unsigned int reg_addr) {
    static struct i2c_rdwr_ioctl_data rdwr;
    static struct i2c_msg msg[2];
    unsigned int reg_width = 2, data_width = 1, reg_step = 1;
    unsigned char buf[4];
    unsigned int data;

    memset(buf, 0x0, 4);

    msg[0].addr = sensor_i2c_addr >> 1;
    msg[0].flags = 0;
    msg[0].len = reg_width;
    msg[0].buf = buf;

    msg[1].addr = sensor_i2c_addr >> 1;
    msg[1].flags = 0;
    msg[1].flags |= I2C_M_RD;
    msg[1].len = data_width;
    msg[1].buf = buf;

    rdwr.msgs = &msg[0];
    rdwr.nmsgs = (__u32)2;

    if (reg_width == 2) {
        buf[0] = (reg_addr >> 8) & 0xff;
        buf[1] = reg_addr & 0xff;
    } else
        buf[0] = reg_addr & 0xff;

    int retval = ioctl(g_fd, I2C_RDWR, &rdwr);
    if (retval != 2) {
        // CMD_I2C_READ error
        retval = -1;
        return -1;
    }

    if (data_width == 2) {
        data = buf[1] | (buf[0] << 8);
    } else
        data = buf[0];

    return data;
}

int detect_sony_sensor() {
    if (sensor_i2c_change_addr(sensor_i2c_addr) < 0)
        return -1;
    // from IMX335 datasheet, p.40
    // 316Ah - 2-6 bits are 1, 7 bit is 0
    int ret316a = sensor_read_register(0x316A);
    if (ret316a > 0 && ((ret316a & 0xfc) == 0x7c)) {
        sprintf(sensor_id, "IMX335");
        return true;
    }

    int ret3013 = sensor_read_register(0x3013);
    if (ret3013 == 64) {
        sprintf(sensor_id, "IMX323");
        return true;
    }

    int ret31dc = sensor_read_register(0x31DC);
    if (ret31dc > 0) {
        if ((ret31dc & 7) <= 1) {
            sprintf(sensor_id, "IMX29%d", ret31dc);
            return true;
        }

        switch (ret31dc & 6) {
        case 4:
            sprintf(sensor_id, "IMX307");
            break;
        case 6:
            sprintf(sensor_id, "IMX327");
            break;
        default:
            sprintf(sensor_id, "IMXXXX");
            return false;
        }
        return true;
    }

    return false;
}

int get_sensor_id() {
    sensor_i2c_init();

    if (detect_sony_sensor()) {
        strcpy(sensor_manufacturer, "Sony");
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
