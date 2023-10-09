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
#include "hal/common.h"
#include "hal/xm.h"
#include "sensors.h"
#include "tools.h"

static read_register_t sensor_read_register;
static write_register_t sensor_write_register;

#define READ_0(addr) sensor_read_register(fd, i2c_addr, addr, 2, 1)
#define READ(addr) READ_0((addr) + 0x3000)

#ifndef STANDALONE_LIBRARY
#define SENSOR_ERR(name, code)                                                 \
    fprintf(stderr, "Error: unexpected value for %s == 0x%x\n", name, code);
#else
#define SENSOR_ERR(name, code)                                                 \
    do {                                                                       \
    } while (0)
#endif

#ifndef STANDALONE_LIBRARY

#define IMX219_TEMP 0x140
static int sony_imx219_tempc(int code) {
    if (code < 6)
        return -10;
    if (code < 12)
        return -5;
    if (code < 18)
        return 0;
    if (code < 24)
        return 5;
    if (code < 30)
        return 10;
    if (code < 36)
        return 15;
    if (code < 43)
        return 20;
    if (code < 49)
        return 25;
    if (code < 55)
        return 30;
    if (code < 61)
        return 35;
    if (code < 67)
        return 40;
    if (code < 73)
        return 45;
    if (code < 79)
        return 50;
    if (code < 85)
        return 55;
    if (code < 91)
        return 60;
    if (code < 97)
        return 65;
    if (code < 104)
        return 70;
    if (code < 110)
        return 75;
    if (code < 116)
        return 80;
    if (code < 122)
        return 85;
    if (code < 128)
        return 90;
    return 95;
}

static void sony_imx219_params(sensor_ctx_t *ctx, int fd,
                               unsigned char i2c_addr) {
    cJSON *j_inner = cJSON_CreateObject();

    sensor_write_register(fd, i2c_addr, IMX219_TEMP, 2, 0x80, 1);
    // https://forums.developer.nvidia.com/t/i2c-regmap-read-issue/154601/4
    usleep(500000);
    int temp = sony_imx219_tempc(READ_0(IMX219_TEMP));
    ADD_PARAM_NUM("temp", temp);
}

static int sony_imx291_fps(u_int8_t frsel, u_int16_t hmax) {
    switch (frsel) {
    case 2:
        // 30/25
        if (hmax == 0x1130)
            return 30;
        else if (hmax == 0x14A0)
            return 25;
        break;
    case 1:
        // 60/50
        if (hmax == 0x0898)
            return 60;
        else if (hmax == 0x0A50)
            return 50;
        break;
    case 0:
        // 120/100
        if (hmax == 0x044C)
            return 120;
        else if (hmax == 0x0528)
            return 100;
        break;
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
                               unsigned char i2c_addr) {
    cJSON *j_inner = cJSON_CreateObject();

    int adbit = READ(0x5) & 1 ? 12 : 10;
    ADD_PARAM_NUM("bitness", adbit);

    ADD_PARAM("databus", sony_imx291_databus((READ(0x46) & 0xf0) >> 4));

    int frsel = (READ(9) & 3);
    int hmax = READ(0x1d) << 8 | READ(0x1c);
    ADD_PARAM_NUM("fps", sony_imx291_fps(frsel, hmax));
    ctx->j_params = j_inner;
}
#endif

static int detect_sony_sensor(sensor_ctx_t *ctx, int fd,
                              unsigned char i2c_addr) {
    if (i2c_change_addr(fd, i2c_addr) < 0)
        return false;

    int chip_id = READ(0x57);
    if (chip_id == 0x06) {
        sprintf(ctx->sensor_id, "IMX347");
        return true;
    }
    // 0x3057 also can be used for IMX335 (chip_id == 0x07)

    if (READ(0x41c) == 0x47) {
        int ret302e = READ(0x02e);
        int ret302f = READ(0x02f);
        if (ret302e == 0x18 && ret302f == 0xf) {
            sprintf(ctx->sensor_id, "IMX334");
            return true;
        }
    }

    // from IMX335 datasheet, p.40
    // 316Ah - 2-6 bits are 1, 7 bit is 0
    int ret16a = READ(0x16A);
    // early break
    if (ret16a == -1)
        return false;

    if (ret16a > 0 && ((ret16a & 0xfc) == 0x7c)) {
        sprintf(ctx->sensor_id, "IMX335");
        return true;
    }

    // Fixed to "40h"
    if (READ(0x13) == 0x40) {

        if (READ(0x4F) == 0x07) {
            sprintf(ctx->sensor_id, "IMX323");
        } else {
            sprintf(ctx->sensor_id, "IMX322");
        }
        return true;
    }

    // from IMX415 datasheet, p.46
    // 3B00h, Set to "2Eh", default value after reset is 28h
    int ret3b00 = READ(0xB00);
    if (ret3b00 == 0x2e || ret3b00 == 0x28) {
        sprintf(ctx->sensor_id, "IMX415");
        return true;
    }

    // Possible check: 3015 == 0x3c
    int ret33b4 = READ(0x3b4);
    if (ret33b4 == 0x96 || ret33b4 == 0xfe) {
        sprintf(ctx->sensor_id, "IMX178");
        return true;
    }

    if (READ(0x120) == 0x80 && READ(0x129) == 0x0d) {
        sprintf(ctx->sensor_id, "IMX274");
        return true;
    }

    // Possible check: 0x303a = 0xc9
    if (READ(0x015) == 0x3c) {
        sprintf(ctx->sensor_id, "IMX185");
        return true;
    }

    if (READ(0x5b8) == 0xfa) {
        sprintf(ctx->sensor_id, "IMX294");
        return true;
    }

    if (READ(0x1c) == 0x8b) {
        sprintf(ctx->sensor_id, "IMX226");
        return true;
    }

    if (READ(0xce) == 0x16) {
        sprintf(ctx->sensor_id, "IMX122");
        return true;
    }

    // IMX326 too?
    if (READ(0x45) == 0x32) {
        sprintf(ctx->sensor_id, "IMX226");
        return true;
    }

    int ret1dc = READ(0x1DC);
    if (ret1dc != 0xff) {
        switch (ret1dc & 6) {
        case 4:
            sprintf(ctx->sensor_id, "IMX307");
            return true;
        case 6:
            sprintf(ctx->sensor_id, "IMX327");
            return true;
        default: {
            if (READ(0x1e) == 0xb2 && READ(0x1f) == 0x1) {
                int ret9c = READ(0x9c);
                switch (ret9c) {
                case 0x20:
                case 0x22:
                    sprintf(ctx->sensor_id, "IMX291");
                    break;
                case 0:
                    sprintf(ctx->sensor_id, "IMX290");
                    break;
                default:
                    SENSOR_ERR("Sony29x", ret9c);
                    return false;
                }
#ifndef STANDALONE_LIBRARY
                sony_imx291_params(ctx, fd, i2c_addr);
#endif
                return true;
            }
        }
        }
    }

    // special case for IMX219 (it has own chip id registers)
    if (READ_0(0) == 0x2 && READ_0(1) == 0x19) {
        sprintf(ctx->sensor_id, "IMX219");
#ifndef STANDALONE_LIBRARY
        sony_imx219_params(ctx, fd, i2c_addr);
#endif
        return true;
    }

    if (READ(0x4) == 0x10) {
        if (READ(0xc) == 0 && READ(0xe) == 0x1) {
            int val_0xd = READ(0xd);
            int val_0x10 = READ(0x10);

            if (val_0xd == 0x20 && val_0x10 == 0x39 && READ(0x6) == 0 &&
                READ(0xf) == 0x1 && READ(0x12) == 0x50) {
                sprintf(ctx->sensor_id, "IMX138");
                return true;
            }

            if (val_0xd == 0 && val_0x10 == 0x1 && READ(0x11) == 0 &&
                READ(0x1e) == 0x1 && READ(0x1f) == 0) {
                if (READ(0x338) != 0) {
                    sprintf(ctx->sensor_id, "IMX385");
                    return true;
                }

                sprintf(ctx->sensor_id, "IMX225");
                return true;
            }
        }

        if (READ(0x9e) == 0x71) {
            sprintf(ctx->sensor_id, "IMX123");
            return true;
        }
    }

    return false;
}

// tested on H42, F22, F23, F37, H62, H65, K05
// TODO(FlyRouter): test on H81
static int detect_soi_sensor(sensor_ctx_t *ctx, int fd,
                             unsigned char i2c_addr) {
    if (i2c_change_addr(fd, i2c_addr) < 0)
        return false;

    // Product ID number (Read only)
    int pid = i2c_read_register(fd, i2c_addr, 0xa, 1, 1);
    // early break
    if (pid == -1)
        return false;

    // Product version number (Read only)
    int ver = i2c_read_register(fd, i2c_addr, 0xb, 1, 1);
    switch (pid) {
    case 0xf:
        sprintf(ctx->sensor_id, "JXF%x", ver);
        return true;
    case 0xa0:
    case 0xa:
        sprintf(ctx->sensor_id, "JXH%x", ver);
        return true;
    case 0x5:
        if (ver == 0x07)
            sprintf(ctx->sensor_id, "JXQ03");
        else
            sprintf(ctx->sensor_id, "JXK%.2x", ver);
        return true;
    case 0x8:
        if (ver == 0x43) {
            sprintf(ctx->sensor_id, "JXQ03P");
            return true;
        } else if (ver == 0x41) {
            sprintf(ctx->sensor_id, "JXF37P");
            return true;
        } else if (ver == 0x42) {
            sprintf(ctx->sensor_id, "JXF53");
            return true;
        } else if (ver == 0x44) {
            sprintf(ctx->sensor_id, "JXF38P");
            return true;
        }
        // fall through
    case 0:
    // it can be another sensor type
    case 0xff:
        return false;
    default:
        SENSOR_ERR("SOI", (pid << 8) + ver);
        return false;
    }
}

// tested on AR0130
static int detect_onsemi_sensor(sensor_ctx_t *ctx, int fd,
                                unsigned char i2c_addr) {
    if (i2c_change_addr(fd, i2c_addr) < 0)
        return false;

    // sensor_write_register(0x301A, 1);
    // msDelay(100);

    int pid = i2c_read_register(fd, i2c_addr, 0x3000, 2, 2);
    int sid = 0;

    switch (pid) {
    case 0x2402:
        sid = 0x0130;
        break;
    case 0x256:
        sid = 0x0237;
        break;
    case 0x2602:
        sid = 0x0331;
        break;
    case 0x2604:
        sid = 0x0330;
        break;
    case 0:
    case 0xffffffff:
    case 0xffff:
        // no response
        break;
    default:
        SENSOR_ERR("Aptina", pid);
        return false;
    }

    if (sid) {
        sprintf(ctx->sensor_id, "AR%04x", sid);
    }
    return sid;
}

static int detect_smartsens_sensor(sensor_ctx_t *ctx, int fd,
                                   unsigned char i2c_addr) {
    if (i2c_change_addr(fd, i2c_addr) < 0)
        return false;

    // could be 0x3005 for SC1035, SC1145, SC1135
    int high = i2c_read_register(fd, i2c_addr, 0x3107, 2, 1);
    // early break
    if (high == -1)
        return false;

    int lower = i2c_read_register(fd, i2c_addr, 0x3108, 2, 1);
    if (lower == -1)
        return false;

    // check for SC1035, SC1145, SC1135 '0x3008' reg val is equal to 0x60

    int res = high << 8 | lower;
    switch (res) {
    case 0x0010:
        // aka fake Aptina AR0130
        res = 0x1035;
        break;
    case 0x1045:
        break;
    case 0x1145:
        break;
    case 0x1235:
        break;
    case 0x1245: {
        int sw = i2c_read_register(fd, i2c_addr, 0x3020, 2, 1);
        sprintf(ctx->sensor_id, "SC2145H_%c", sw == 2 ? 'A' : 'B');
        return true;
    }
    case 0x2032:
        res = 0x2035;
        break;
    case 0x2045:
        break;
    case 0x2135:
        break;
    case 0x2145:
        break;
    case 0x2232: {
        if (i2c_read_register(fd, i2c_addr, 0x3109, 2, 1) == 0x20)
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
        res = 0x1145;
        break;
    case 0x2300:
        // XM530
        strcpy(ctx->sensor_id, "SC307P");
        return true;
    case 0x2310:
        break;
    case 0x2311:
        // XM
        strcpy(ctx->sensor_id, "SC2315");
        return true;
    case 0x2330:
        strcpy(ctx->sensor_id, "SC307H");
    case 0x3035:
        break;
    case 0x3235:
        res = 0x4236;
        break;
    case 0x4210:
        break;
    case 0x4235:
        res = 0x4238;
        break;
    case 0x5235:
        break;
    case 0x5300:
        strcpy(ctx->sensor_id, "SC335E");
        return true;
    case 0xbd2f:
        strcpy(ctx->sensor_id, "SC450AI");
        return true;
    case 0xca13:
        // XM530
        strcpy(ctx->sensor_id, "SC1335T");
        return true;
    case 0xca18:
        // XM530
        strcpy(ctx->sensor_id, "SC1330T");
        return true;
    case 0xcb07:
        // aka SC307C
        strcpy(ctx->sensor_id, "SC2232H");
        return true;
    case 0xcb08:
        res = 0x2320;
        break;
    case 0xcb10:
        res = 0x2239;
        break;
    case 0xcb14:
        res = 0x2335;
        break;
    case 0xcb17:
        res = 0x2232;
        break;
    case 0xcb1c:
        strcpy(ctx->sensor_id, "SC337H");
        return true;
    case 0xcc05:
        // AKA AUGE
        res = 0x3235;
        break;
    case 0xcc1a:
        res = 0x3335;
        break;
    case 0xcc40:
        strcpy(ctx->sensor_id, "SC301IoT");
        return true;
    case 0xcc41:
        // XM530
        res = 0x3338;
        break;
    case 0xcd01:
        // XM
        strcpy(ctx->sensor_id, "SC4335P");
        return true;
    case 0xcb3a:
        // XM530
        res = 0x2336;
        break;
    case 0xcb3e:
        // XM
        strcpy(ctx->sensor_id, "SC223A");
        return true;
    case 0xcd2e:
        // XM
        strcpy(ctx->sensor_id, "SC401AI");
        return true;
    case 0xce1a:
        // XM
        res = 0x5332;
        break;
    case 0xce1f:
        // XM
        strcpy(ctx->sensor_id, "SC501AI");
        return true;
    case 0xda23:
        // XM 530
        res = 0x1345;
        return true;
    case 0xdc42:
        res = 0x4336;
        return true;
    case 0:
    case 0xffff:
        // SC1135 catches here
        return false;
    default:
        SENSOR_ERR("SmartSens", res);
        return false;
    }

    sprintf(ctx->sensor_id, "SC%04x", res);
    return true;
}

static int detect_omni_sensor(sensor_ctx_t *ctx, int fd,
                              unsigned char i2c_addr) {
    int prod_msb;
    int prod_lsb;
    int res;

    if (i2c_change_addr(fd, i2c_addr) < 0)
        return false;

    // HISI_V2 needs width 2. Old OmniVision sensors do not provide mfg_id
    // register.
    prod_msb = i2c_read_register(fd, i2c_addr, 0x300A, 2, 1);
    prod_lsb = i2c_read_register(fd, i2c_addr, 0x300B, 2, 1);
    res = prod_msb << 8 | prod_lsb;

    switch (res) {
    case 0x2710:
    case 0x2715:
    case 0x2718:
    case 0x5647:
    case 0x9732:
        sprintf(ctx->sensor_id, "OV%04x", res);
        return true;
    case 0x4688:
        sprintf(ctx->sensor_id, "OV4689");
        return true;
    case 0x5303:
        sprintf(ctx->sensor_id, "SP4329");
        return true;
    case 0x5305:
        sprintf(ctx->sensor_id, "OS05A");
        return true;
    case 0x5308:
        sprintf(ctx->sensor_id, "OS08A");
        return true;
    default:
        break;
    }

    // Check OmniVision ManufacturerID
    int mfg_msb = i2c_read_register(fd, i2c_addr, 0x301C, 1, 1);
    int mfg_lsb = i2c_read_register(fd, i2c_addr, 0x301D, 1, 1);
    if (mfg_msb == -1 || mfg_lsb == -1 || mfg_msb != 0x7f || mfg_lsb != 0xa2)
        return false;

    prod_msb = i2c_read_register(fd, i2c_addr, 0x300A, 1, 1);
    // early break
    if (prod_msb == -1)
        return false;

    prod_lsb = i2c_read_register(fd, i2c_addr, 0x300B, 1, 1);
    if (prod_lsb == -1)
        return false;

    res = prod_msb << 8 | prod_lsb;

    // skip empty result
    if (!res)
        return false;

    // model mapping
    switch (res) {
    case 0x2770:
        res = 0x2718;
        break;
    case 0x4688:
        res = 0x4689;
        break;
    case 0x9711:
        res = 0x9712;
        break;
    case 0x2710:
    case 0x2715:
    case 0x9732:
    case 0x9750:
        // for models with identical ID for model name
        break;
    case 0x5305:
        sprintf(ctx->sensor_id, "OS05A");
        break;
    case 0:
    case 0xffff:
        return false;
    default:
        SENSOR_ERR("OmniVision", res);
        return false;
    }
    sprintf(ctx->sensor_id, "OV%04x", res);

    return true;
}

static int detect_galaxycore_sensor(sensor_ctx_t *ctx, int fd,
                                    unsigned char i2c_addr) {
    if (i2c_change_addr(fd, i2c_addr) < 0)
        return false;

    int prod_msb = i2c_read_register(fd, i2c_addr, 0x3f0, 2, 1);
    int prod_lsb = i2c_read_register(fd, i2c_addr, 0x3f1, 2, 1);

    if (prod_msb == -1 || prod_lsb == -1) {
        prod_msb = i2c_read_register(fd, i2c_addr, 0xf0, 1, 1);
        prod_lsb = i2c_read_register(fd, i2c_addr, 0xf1, 1, 1);
    }

    if (prod_msb == -1 || prod_lsb == -1)
        return false;

    int res = prod_msb << 8 | prod_lsb;

    switch (res) {
    case 0x2053:
    case 0x2083:
    case 0x4023:
    case 0x4653:
        sprintf(ctx->sensor_id, "GC%04x", res);
        return true;
    }

    prod_msb = i2c_read_register(fd, i2c_addr, 0xf0, 1, 1);
    if (prod_msb == -1)
        return false;

    prod_lsb = i2c_read_register(fd, i2c_addr, 0xf1, 1, 1);
    if (prod_lsb == -1)
        return false;
    res = prod_msb << 8 | prod_lsb;

    if (!res)
        return false;

    switch (res) {
    case 0x1004:
    case 0x1024:
    case 0x1034:
    case 0x1054:
    case 0x2023:
    case 0x2033:
    case 0x2053:
    case 0x2063:
    case 0x2083:
    case 0x2093:
    case 0x3003:
    case 0x4023:
    case 0x4653:
    case 0x46c3:
    case 0x5035:
        sprintf(ctx->sensor_id, "GC%04x", res);
        return true;
    case 0xffff:
        // no response
        return false;
    default:
        SENSOR_ERR("GalaxyCore", res);
        return false;
    }
}

static int detect_superpix_sensor(sensor_ctx_t *ctx, int fd,
                                  unsigned char i2c_addr) {
    if (i2c_change_addr(fd, i2c_addr) < 0)
        return false;

    int prod_msb = i2c_read_register(fd, i2c_addr, 0x02, 1, 1);
    if (prod_msb == -1)
        return false;

    int prod_lsb = i2c_read_register(fd, i2c_addr, 0x03, 1, 1);
    if (prod_lsb == -1)
        return false;

    int res = prod_msb << 8 | prod_lsb;

    if (!res)
        return false;

    switch (res) {
    // Omnivision-SuperPix OV2735
    case 0x2735:
        sprintf(ctx->sensor_id, "OV%04x", res);
        return res;
        break;
    }

    prod_msb = i2c_read_register(fd, i2c_addr, 0xfa, 1, 1);
    // early break
    if (prod_msb == -1)
        return false;

    prod_lsb = i2c_read_register(fd, i2c_addr, 0xfb, 1, 1);
    if (prod_lsb == -1)
        return false;
    res = prod_msb << 8 | prod_lsb;

    if (!res)
        return false;

    switch (res) {
    // hax, sensor doesnt seem to have id register
    case 0x2073:
    case 0x0000:
        res = 0x2305;
        break;
    case 0xffff:
        // no response
        return false;
    default:
        SENSOR_ERR("SuperPix", res);
        return false;
    }

    if (res) {
        sprintf(ctx->sensor_id, "SP%04x", res);
    }
    return res;
}

static int detect_possible_sensors(sensor_ctx_t *ctx, int fd,
                                   int (*detect_fn)(sensor_ctx_t *ctx, int,
                                                    unsigned char),
                                   int type) {
    if (possible_i2c_addrs == NULL)
        return false;

    sensor_addr_t *sdata = possible_i2c_addrs;

    while (sdata->sensor_type) {
        if (sdata->sensor_type == type) {
            unsigned char *addr = sdata->addrs;
            while (*addr) {
                if (detect_fn(ctx, fd, *addr)) {
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
    int fd = open_i2c_sensor_fd();
    if (fd == -1)
        return false;

    sensor_read_register = i2c_read_register;
    sensor_write_register = i2c_write_register;
    if (detect_possible_sensors(ctx, fd, detect_soi_sensor, SENSOR_SOI)) {
        strcpy(ctx->vendor, "Silicon Optronics");
        ctx->reg_width = 1;
        detected = true;
    } else if (detect_possible_sensors(ctx, fd, detect_onsemi_sensor,
                                       SENSOR_ONSEMI)) {
        strcpy(ctx->vendor, "ON Semiconductor");
        ctx->data_width = 2;
        detected = true;
    } else if (detect_possible_sensors(ctx, fd, detect_omni_sensor,
                                       SENSOR_OMNIVISION)) {
        strcpy(ctx->vendor, "OmniVision");
        detected = true;
    } else if (detect_possible_sensors(ctx, fd, detect_sony_sensor,
                                       SENSOR_SONY)) {
        strcpy(ctx->vendor, "Sony");
        detected = true;
    } else if (detect_possible_sensors(ctx, fd, detect_smartsens_sensor,
                                       SENSOR_SMARTSENS)) {
        strcpy(ctx->vendor, "SmartSens");
        detected = true;
    } else if (detect_possible_sensors(ctx, fd, detect_galaxycore_sensor,
                                       SENSOR_GALAXYCORE)) {
        strcpy(ctx->vendor, "GalaxyCore");
        ctx->reg_width = 1;
        detected = true;
    } else if (detect_possible_sensors(ctx, fd, detect_superpix_sensor,
                                       SENSOR_SUPERPIX)) {
        strcpy(ctx->vendor, "SuperPix");
        ctx->reg_width = 1;
        detected = true;
    }
exit:
    close_sensor_fd(fd);
    hal_cleanup();
    return detected;
}

static int dummy_change_addr(int fd, unsigned char addr) {}

static bool get_sensor_id_spi(sensor_ctx_t *ctx) {
    if (open_spi_sensor_fd == NULL || spi_read_register == NULL)
        return false;

    int fd = open_spi_sensor_fd();
    if (fd < 0)
        return false;

    sensor_read_register = spi_read_register;
    i2c_change_addr = dummy_change_addr;

    int res = detect_sony_sensor(ctx, fd, 0);
    if (res) {
        strcpy(ctx->vendor, "Sony");
    }
    close(fd);
    return res;
}

bool getsensorid(sensor_ctx_t *ctx) {
    if (!getchipname())
        return NULL;
    // there is no platform specific i2c/spi access layer
    if (!open_i2c_sensor_fd)
        return NULL;

    // Use common settings as default
    ctx->data_width = 1;
    ctx->reg_width = 2;

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

    cJSON *j_sensors = cJSON_CreateArray();
    ctx.j_sensor = cJSON_CreateObject();
    cJSON *j_inner = ctx.j_sensor;
    cJSON_AddItemToArray(j_sensors, j_inner);

    if (!getsensorid(&ctx)) {
        cJSON_Delete(j_sensors);
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

    return j_sensors;
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
