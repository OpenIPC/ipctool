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
#include <sys/types.h>
#include <unistd.h>

#include "chipid.h"
#include "cjson/cJSON.h"
#include "hal_common.h"
#include "hal_xm.h"
#include "sensors.h"
#include "tools.h"

#define READ(addr) sensor_read_register(fd, i2c_addr, base + addr, 2, 1)

#ifndef STANDALONE_LIBRARY

static int sony_imx291_fps(u_int8_t frsel, u_int16_t hmax) {
    switch (frsel) {
    case 2:
        // 30/25
        if (hmax == 0x1130)
            return 30;
        else if (hmax == 0x14A0)
            return 25;
    case 1:
        // 60/50
        if (hmax == 0x0898)
            return 60;
        else if (hmax == 0x0A50)
            return 50;
    case 0:
        // 120/100
        if (hmax == 0x044C)
            return 120;
        else if (hmax == 0x0528)
            return 100;
    }
    return 0;
}

static const char *sony_imx291_databus(int odbit) {
    switch (odbit) {
    case 0:
        return "Parallel CMOS SDR";
    case 0xD:
        return "LVDS 2 ch";
    case 0xE:
        return "LVDS 4 ch";
    case 0xF:
        return "LVDS 8 ch";
    default:
        return NULL;
    }
}

static void sony_imx291_params(sensor_ctx_t *ctx, int fd,
                               unsigned char i2c_addr, unsigned int base) {
    cJSON *j_inner = cJSON_CreateObject();

    int adbit = READ(0x5) & 1 ? 12 : 10;
    ADD_PARAM_NUM("bitness", adbit);

    ADD_PARAM("databus", sony_imx291_databus((READ(0x46) & 0xf0) >> 4));

    int frsel = (READ(9) & 3);
    int hmax = READ(0x1d) << 8 | READ(0x1c);
    ADD_PARAM_NUM("fps", sony_imx291_fps(frsel, hmax))
    ctx->j_params = j_inner;
}

#endif

static int detect_sony_sensor(sensor_ctx_t *ctx, int fd, unsigned char i2c_addr,
                              unsigned int base) {
    if (sensor_i2c_change_addr(fd, i2c_addr) < 0)
        return false;

    // from IMX335 datasheet, p.40
    // 316Ah - 2-6 bits are 1, 7 bit is 0
    int ret16a = sensor_read_register(fd, i2c_addr, base + 0x16A, 2, 1);
    // early break
    if (ret16a == -1)
        return false;

    if (ret16a > 0 && ((ret16a & 0xfc) == 0x7c)) {
        sprintf(ctx->sensor_id, "IMX335");
        return true;
    }

    // Fixed to "40h"
    int ret13 = sensor_read_register(fd, i2c_addr, base + 0x13, 2, 1);
    if (ret13 == 0x40) {

        int ret4F = sensor_read_register(fd, i2c_addr, base + 0x4F, 2, 1);
        if (ret4F == 0x07) {
            sprintf(ctx->sensor_id, "IMX323");
        } else {
            sprintf(ctx->sensor_id, "IMX322");
        }
        return true;
    }

    // from IMX415 datasheet, p.46
    // 3B00h, Set to "2Eh", default value after reset is 28h
    int ret3b00 = sensor_read_register(fd, i2c_addr, base + 0xB00, 2, 1);
    if (ret3b00 == 0x2e || ret3b00 == 0x28) {
        sprintf(ctx->sensor_id, "IMX415");
        return true;
    }

    int ret1dc = sensor_read_register(fd, i2c_addr, base + 0x1DC, 2, 1);
    if (ret1dc != 0xff) {
        switch (ret1dc & 6) {
        case 4:
            sprintf(ctx->sensor_id, "IMX307");
            return true;
        case 6:
            sprintf(ctx->sensor_id, "IMX327");
            return true;
        default: {
            int ret3010 = sensor_read_register(fd, i2c_addr, base + 0x10, 2, 1);
            if (ret3010 == 0x21) {
                sprintf(ctx->sensor_id, "IMX29%d", ret1dc & 7);
#ifndef STANDALONE_LIBRARY
                sony_imx291_params(ctx, fd, i2c_addr, base);
#endif
                return true;
            }
        }
        }
    }

    int ret31e0 = sensor_read_register(fd, i2c_addr, base + 0x1E0, 2, 1);
    if (ret31e0 > 0) {
        uint8_t val = (0xc0 & ret31e0) >> 6;
        if (val == 3) {
            sprintf(ctx->sensor_id, "IMX224");
            return true;
        } else if (val == 0) {
            sprintf(ctx->sensor_id, "IMX225");
            return true;
        }
    }

    return false;
}

// tested on F22, F23, F37, H62, H65, K05
// TODO(FlyRouter): test on H42, H81
static int detect_soi_sensor(sensor_ctx_t *ctx, int fd, unsigned char i2c_addr,
                             unsigned int base) {
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
        sprintf(ctx->sensor_id, "JXF%x", ver);
        return true;
    case 0xa0:
    case 0xa:
        sprintf(ctx->sensor_id, "JXH%x", ver);
        return true;
    case 0x5:
        sprintf(ctx->sensor_id, "JXK%.2x", ver);
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
static int detect_onsemi_sensor(sensor_ctx_t *ctx, int fd,
                                unsigned char i2c_addr, unsigned int base) {
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
        sprintf(ctx->sensor_id, "AR%04x", sid);
    }
    return sid;
}

static int detect_smartsens_sensor(sensor_ctx_t *ctx, int fd,
                                   unsigned char i2c_addr, unsigned int base) {
    if (sensor_i2c_change_addr(fd, i2c_addr) < 0)
        return false;

    // xm_i2c_write(0x103, 1); // reset all registers (2 times and then delay)
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
    case 0x1235:
        // Untested
        break;
    case 0x1245:
        // Untested
        {
            int sw = sensor_read_register(fd, i2c_addr, 0x3020, 2, 1);
            sprintf(ctx->sensor_id, "SC2145H_%c", sw == 2 ? 'A' : 'B');
            return true;
        }
    case 0x2032:
    case 0x2135:
        break;
    case 0x2145:
        // Untested
        break;
    case 0x2045:
        break;
    case 0x2210:
        // (Untested) aka fake Aptina AR0130
        res = 0x1035;
        break;
    case 0x2232: {
        if (sensor_read_register(fd, i2c_addr, 0x3109, 2, 1) == 0x20)
            strcpy(ctx->sensor_id, "SC2235E");
        else // 0x01
            strcpy(ctx->sensor_id, "SC2235P");
        return true;
    }
    case 0x2235:
        break;
    case 0x2238:
        // aka SC4239Р and SC307E
        strcpy(ctx->sensor_id, "SC2315E");
        return true;
    case 0x2245:
        // Untested
        res = 0x1145;
        break;
    case 0x2311:
        res = 0x2315;
        break;
    case 0x2330:
        // Untested
        strcpy(ctx->sensor_id, "SC307H");
    case 0x3035:
        break;
    case 0x3235:
        res = 0x4236;
        break;
    case 0x5235:
        // Untested
        break;
    case 0x5300:
        strcpy(ctx->sensor_id, "SC335E");
        return true;
    case 0xcb07:
        strcpy(ctx->sensor_id, "SC2232H");
        return true;
    case 0xcb10:
        res = 0x2239;
        break;
    case 0xcb14:
        // Untested
        res = 0x2335;
        break;
    case 0xcb17:
        res = 0x2232;
        break;
    case 0xcc05:
        // Untested
        strcpy(ctx->sensor_id, "AUGE");
        return true;
    case 0xcc1a:
        // Untested
        strcpy(ctx->sensor_id, "SC3335");
        return true;
    case 0xcd01:
        // Untested
        strcpy(ctx->sensor_id, "SC4335P");
        return true;
    case 0xCD2E:
        // Untested
        strcpy(ctx->sensor_id, "SC401AI");
        return true;
    case 0xCE1F:
        // Untested
        strcpy(ctx->sensor_id, "SC501AI");
        return true;
    case 0:
    case 0xffff:
        // SC1135 catches here
        return false;
    default:
        fprintf(stderr, "Error: unexpected value for SmartSens == 0x%x\n", res);
        return false;
    }

    sprintf(ctx->sensor_id, "SC%04x", res);
    return true;
}

// TODO(FlyRouter): test on OV9732
static int detect_omni_sensor(sensor_ctx_t *ctx, int fd, unsigned char i2c_addr,
                              unsigned int base) {
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
    sprintf(ctx->sensor_id, "OV%04x", res);

    return true;
}

static int detect_possible_sensors(sensor_ctx_t *ctx, int fd,
                                   int (*detect_fn)(sensor_ctx_t *ctx, int,
                                                    unsigned char,
                                                    unsigned int base),
                                   int type, unsigned int base) {
    sensor_addr_t *sdata = possible_i2c_addrs;

    while (sdata->sensor_type) {
        if (sdata->sensor_type == type) {
            unsigned char *addr = sdata->addrs;
            while (*addr) {
                if (detect_fn(ctx, fd, *addr, base)) {
                    ctx->addr = *addr;
                    return true;
                };
                addr++;
            }
        }
        sdata++;
    }
    return false;
}

static bool get_sensor_id_i2c(sensor_ctx_t *ctx) {
    bool detected = false;
    int fd = open_sensor_fd();
    if (fd == -1)
        return false;

    if (detect_possible_sensors(ctx, fd, detect_soi_sensor, SENSOR_SOI, 0)) {
        strcpy(ctx->vendor, "Silicon Optronics");
        detected = true;
    } else if (detect_possible_sensors(ctx, fd, detect_onsemi_sensor,
                                       SENSOR_ONSEMI, 0)) {
        strcpy(ctx->vendor, "ON Semiconductor");
        detected = true;
    } else if (detect_possible_sensors(ctx, fd, detect_sony_sensor, SENSOR_SONY,
                                       0x3000)) {
        strcpy(ctx->vendor, "Sony");
        detected = true;
    } else if (detect_possible_sensors(ctx, fd, detect_omni_sensor,
                                       SENSOR_OMNIVISION, 0)) {
        strcpy(ctx->vendor, "OmniVision");
        detected = true;
    } else if (detect_possible_sensors(ctx, fd, detect_smartsens_sensor,
                                       SENSOR_SMARTSENS, 0)) {
        strcpy(ctx->vendor, "SmartSens");
        detected = true;
    }

exit:
    close_sensor_fd(fd);
    hal_cleanup();
    return detected;
}

static int dummy_change_addr(int fd, unsigned char addr) {}

static bool get_sensor_id_spi(sensor_ctx_t *ctx) {
    int fd = -1;

    // fallback for SPI implemented only for HiSilicon
    if (chip_generation == HISI_V1) {
        fd = open("/dev/ssp", 0);
        sensor_read_register = sony_ssp_read_register;
    } else if (chip_generation == HISI_V3 || chip_generation == HISI_V2) {
        fd = open("/dev/spidev0.0", 0);
        sensor_read_register = hisi_gen3_spi_read_register;
    } else
        return false;
    if (fd < 0)
        return false;

    sensor_i2c_change_addr = dummy_change_addr;

    int res = detect_sony_sensor(ctx, fd, 0, 0x200);
    if (res) {
        strcpy(ctx->vendor, "Sony");
    }
    close(fd);
    return res;
}

static bool getsensorid(sensor_ctx_t *ctx) {
    if (!getchipid())
        return NULL;
    // there is no platform specific i2c/spi access layer
    if (!open_sensor_fd)
        return NULL;

    bool i2c_detected = get_sensor_id_i2c(ctx);
    if (i2c_detected) {
        strcpy(ctx->control, "i2c");
        return true;
    }

    bool spi_detected = get_sensor_id_spi(ctx);
    if (spi_detected) {
        strcpy(ctx->control, "spi");
    }
    return spi_detected;
}

#ifndef STANDALONE_LIBRARY

cJSON *detect_sensors() {
    sensor_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    cJSON *fake_root = cJSON_CreateObject();
    cJSON *j_sensors = cJSON_AddArrayToObject(fake_root, "sensors");
    ctx.j_sensor = cJSON_CreateObject();
    cJSON *j_inner = ctx.j_sensor;
    cJSON_AddItemToArray(j_sensors, j_inner);

    if (!getsensorid(&ctx)) {
        cJSON_Delete(fake_root);
        return NULL;
    }

    ADD_PARAM("vendor", ctx.vendor);
    ADD_PARAM("model", ctx.sensor_id);
    {
        cJSON *j_inner = cJSON_CreateObject();
        cJSON_AddItemToObject(ctx.j_sensor, "control", j_inner);
        ADD_PARAM_NUM("bus", 0);
        ADD_PARAM("type", ctx.control);
        if (ctx.addr)
            ADD_PARAM_FMT("addr", "0x%x", ctx.addr);
        if (ctx.j_params)
            cJSON_AddItemToObject(ctx.j_sensor, "params", ctx.j_params);

        hisi_vi_information(&ctx);
    }

    return fake_root;
}

#endif

static char sensor_indentity[16];
const char *getsensoridentity() {
    sensor_ctx_t ctx;
    if (!getsensorid(&ctx))
        return NULL;
    lsnprintf(sensor_indentity, sizeof(sensor_indentity), "%s_%s",
              ctx.sensor_id, ctx.control);
    return sensor_indentity;
}

const char *getsensorshort() {
    sensor_ctx_t ctx;
    if (!getsensorid(&ctx))
        return NULL;
    lsnprintf(sensor_indentity, sizeof(sensor_indentity), "%s", ctx.sensor_id);
    return sensor_indentity;
}
