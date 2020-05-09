#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <linux/ioctl.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/resource.h>

#include "chipid.h"
#include "hal_common.h"
#include "sensorid.h"

char sensor_id[128];
char sensor_manufacturer[128];

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

// tested on AR0130
int detect_onsemi_sensor(int fd) {
    const unsigned char i2c_addr = 0x10;
    if (sensor_i2c_change_addr(fd, i2c_addr) < 0)
        return false;

    int pid = sensor_read_register(fd, i2c_addr, 0x3000, 2, 2);
    int sid = 0;

    switch (pid) {
    case 0x2402:
        sid = 0x0130;
        break;
    case 0x256:
        sid = 0x0237;
        break;
    // no response
    case 0xffffffff:
        break;
    default:
        fprintf(stderr, "Error: unexpected value for Aptina == %x\n", pid);
    }

    if (sid) {
        sprintf(sensor_id, "AR%04x", sid);
    }
    return sid;
}

bool get_sensor_id() {
    // if system wasn't detected previously
    if (!strcmp("error", chip_id)) {
        get_system_id();
    }

    int fd = open_sensor_fd();

    if (detect_soi_sensor(fd)) {
        strcpy(sensor_manufacturer, "Silicon Optronics");
        return true;
    } else if (detect_onsemi_sensor(fd)) {
        strcpy(sensor_manufacturer, "ON Semiconductor");
        return true;
    } else if (detect_sony_sensor(fd)) {
        strcpy(sensor_manufacturer, "Sony");
        return true;
    }
    return false;
}
