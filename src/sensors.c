#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <linux/ioctl.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <unistd.h>

#include "chipid.h"
#include "hal_common.h"
#include "sensors.h"

char sensor_id[128];
char sensor_manufacturer[128];
char control[128];

int detect_sony_sensor(int fd, unsigned char i2c_addr, unsigned int base) {
    if (sensor_i2c_change_addr(fd, i2c_addr) < 0)
        return false;

    // from IMX335 datasheet, p.40
    // 316Ah - 2-6 bits are 1, 7 bit is 0
    int ret16a = sensor_read_register(fd, i2c_addr, base + 0x16A, 2, 1);
    // early break
    if (ret16a == -1)
        return false;

    if (ret16a > 0 && ((ret16a & 0xfc) == 0x7c)) {
        sprintf(sensor_id, "IMX335");
        return true;
    }

    // Fixed to "40h"
    int ret13 = sensor_read_register(fd, i2c_addr, base + 0x13, 2, 1);
    if (ret13 == 0x40) {

        int ret4F = sensor_read_register(fd, i2c_addr, base + 0x4F, 2, 1);
        if (ret4F == 0x07) {
            sprintf(sensor_id, "IMX323");
        } else {
            sprintf(sensor_id, "IMX322");
        }
        return true;
    }

    int ret1dc = sensor_read_register(fd, i2c_addr, base + 0x1DC, 2, 1);
    if (ret1dc > 0 && ret1dc != 0xff) {
        switch (ret1dc & 6) {
        case 4:
            sprintf(sensor_id, "IMX307");
            break;
        case 6:
            sprintf(sensor_id, "IMX327");
            break;
        default:
            sprintf(sensor_id, "IMX29%d", ret1dc & 7);
            return true;
        }
        return true;
    }

    return false;
}

// tested on F22, F23, F37, H62, H65
// TODO(FlyRouter): test on H42, H81
int detect_soi_sensor(int fd, unsigned char i2c_addr, unsigned int base) {
    if (sensor_i2c_change_addr(fd, i2c_addr) < 0)
        return false;

    // Product ID number (Read only)
    int pid = sensor_read_register(fd, i2c_addr, 0xa, 1, 1);
    // early break
    if (pid == -1)
        return false;

    // Product version number (Read only)
    int ver = sensor_read_register(fd, i2c_addr, 0xb, 1, 1);
    switch (pid) {
    case 0xf:
        sprintf(sensor_id, "JXF%x", ver);
        return true;
    case 0xa0:
    case 0xa:
        sprintf(sensor_id, "JXH%x", ver);
        return true;
    // it can be another sensor type
    case 0:
    case 0xff:
        return false;
    default:
        fprintf(stderr, "Error: unexpected value for SOI == 0x%x\n",
                (pid << 8) + ver);
        return false;
    }
}

// tested on AR0130
int detect_onsemi_sensor(int fd, unsigned char i2c_addr, unsigned int base) {
    if (sensor_i2c_change_addr(fd, i2c_addr) < 0)
        return false;

    // sensor_write_register(0x301A, 1);
    // msDelay(100);

    int pid = sensor_read_register(fd, i2c_addr, 0x3000, 2, 2);
    int sid = 0;

    switch (pid) {
    case 0x2402:
        sid = 0x0130;
        break;
    case 0x256:
        sid = 0x0237;
        break;
    case 0:
    case 0xffffffff:
    case 0xffff:
        // no response
        break;
    default:
        fprintf(stderr, "Error: unexpected value for Aptina == 0x%x\n", pid);
    }

    if (sid) {
        sprintf(sensor_id, "AR%04x", sid);
    }
    return sid;
}

int detect_smartsens_sensor(int fd, unsigned char i2c_addr, unsigned int base) {
    if (sensor_i2c_change_addr(fd, i2c_addr) < 0)
        return false;

    // xm_i2c_write(0x103, 1);
    // msDelay(100);

    // could be 0x3005 for SC1035, SC1145, SC1135
    int high = sensor_read_register(fd, i2c_addr, 0x3107, 2, 1);
    // early break
    if (high == -1)
        return false;

    int lower = sensor_read_register(fd, i2c_addr, 0x3108, 2, 1);
    if (lower == -1)
        return false;

    // check for SC1035, SC1145, SC1135 '0x3008' reg val is equal to 0x60

    int res = high << 8 | lower;
    switch (res) {
    case 0x2032:
        res = 0x2135;
        break;
    case 0x2045:
        break;
    // Untested
    case 0x2210:
        res = 0x1035;
        break;
    case 0x2232:
        strcpy(sensor_id, "SC2235P");
        return true;
    case 0x2235:
        break;
    case 0x2238:
        strcpy(sensor_id, "SC2315E");
        return true;
    // Untested
    case 0x2245:
        res = 0x1145;
        break;
    case 0x2311:
        res = 0x2315;
        break;
    // Untested
    case 0x3235:
        res = 0x5239;
        break;
    // Untested
    case 0x5235:
        break;
    case 0x5300:
        break;
    case 0:
        // SC1135 catches here
        return false;
    default:
        fprintf(stderr, "Error: unexpected value for SmartSens == 0x%x\n", res);
        return false;
    }

    sprintf(sensor_id, "SC%04x", res);
    return true;
}

// TODO(FlyRouter): test on OV9732
int detect_omni_sensor(int fd, unsigned char i2c_addr, unsigned int base) {
    if (sensor_i2c_change_addr(fd, i2c_addr) < 0)
        return false;

    // sensor_write_register(0x103, 1);
    // sensor_read_register(0x302A) != 0xA0

    int prod_msb = sensor_read_register(fd, i2c_addr, 0x300A, 1, 1);
    // early break
    if (prod_msb == -1)
        return false;

    int prod_lsb = sensor_read_register(fd, i2c_addr, 0x300B, 1, 1);
    if (prod_lsb == -1)
        return false;
    int res = prod_msb << 8 | prod_lsb;

    // skip empty result
    if (!res)
        return false;

    // model mapping
    switch (res) {
    case 0x9711:
        res = 0x9712;
        break;
    case 0x9732:
    case 0x9750:
        // for models with identical ID for model name
        break;
    case 0:
    case 0xffff:
        return false;
    default:
        fprintf(stderr, "Error: unexpected value for OmniVision == 0x%x\n",
                res);
        return false;
    }
    sprintf(sensor_id, "OV%04x", res);

    return true;
}

int detect_possible_sensors(int fd,
                            int (*detect_fn)(int, unsigned char,
                                             unsigned int base),
                            int type, unsigned int base) {
    sensor_addr_t *sdata = possible_i2c_addrs;

    while (sdata->sensor_type) {
        if (sdata->sensor_type == type) {
            unsigned char *addr = sdata->addrs;
            while (*addr) {
                if (detect_fn(fd, *addr, base)) {
                    return true;
                };
                addr++;
            }
        }
        sdata++;
    }
    return false;
}

static bool get_sensor_id_i2c() {
    int fd = open_sensor_fd();

    if (detect_possible_sensors(fd, detect_soi_sensor, SENSOR_SOI, 0)) {
        strcpy(sensor_manufacturer, "Silicon Optronics");
        return true;
    } else if (detect_possible_sensors(fd, detect_onsemi_sensor, SENSOR_ONSEMI,
                                       0)) {
        strcpy(sensor_manufacturer, "ON Semiconductor");
        return true;
    } else if (detect_possible_sensors(fd, detect_sony_sensor, SENSOR_SONY,
                                       0x3000)) {
        strcpy(sensor_manufacturer, "Sony");
        return true;
    } else if (detect_possible_sensors(fd, detect_omni_sensor,
                                       SENSOR_OMNIVISION, 0)) {
        strcpy(sensor_manufacturer, "OmniVision");
        return true;
    } else if (detect_possible_sensors(fd, detect_smartsens_sensor,
                                       SENSOR_SMARTSENS, 0)) {
        strcpy(sensor_manufacturer, "SmartSens");
        return true;
    }

    close(fd);
    return false;
}

int dummy_change_addr(int fd, unsigned char addr) {}

static bool get_sensor_id_spi() {
    int fd = -1;

    // fallback for SPI implemented only for HiSilicon
    if (chip_generation == 0x35180100) {
        fd = open("/dev/ssp", 0);
        sensor_read_register = sony_ssp_read_register;
    } else if (chip_generation == 0x3516C300 || chip_generation == 0x3518E200) {
        fd = open("/dev/spidev0.0", 0);
        sensor_read_register = hisi_gen3_spi_read_register;
    } else
        return false;
    if (fd < 0)
        return false;

    sensor_i2c_change_addr = dummy_change_addr;

    int res = detect_sony_sensor(fd, 0, 0x200);
    if (res) {
        strcpy(sensor_manufacturer, "Sony");
    }
    close(fd);
    return res;
}

bool get_sensor_id() {
    // if system wasn't detected previously
    if (!*chip_id) {
        get_system_id();
    }

    bool i2c_detected = get_sensor_id_i2c();
    if (i2c_detected) {
        strcpy(control, "i2c");
        return true;
    }

    bool spi_detected = get_sensor_id_spi();
    if (spi_detected) {
        strcpy(control, "spi");
    }
    return spi_detected;
}

const char *get_sensor_data_type() {
    switch (chip_generation) {
    case 0x35180100:
        return hisi_cv100_get_sensor_data_type();
    case 0x3518E200:
        return hisi_cv200_get_sensor_data_type();
    case 0x3516C300:
        return hisi_cv300_get_sensor_data_type();
    case 0x3516E300:
        return hisi_ev300_get_sensor_data_type();
    default:
        return NULL;
    }
}

const char *get_sensor_clock() {
    switch (chip_generation) {
    case 0x35180100:
        return hisi_cv100_get_sensor_clock();
    case 0x3518E200:
        return hisi_cv200_get_sensor_clock();
    case 0x3516C300:
        return hisi_cv300_get_sensor_clock();
    case 0x3516E300:
        return hisi_ev300_get_sensor_clock();
    default:
        return NULL;
    }
}
