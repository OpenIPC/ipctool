#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
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

#include "chipid.h"
#include "cjson/cJSON.h"
#include "hal/common.h"
#include "hal/hisi/ethernet.h"
#include "hal/hisi/hal_hisi.h"
#include "hal/hisi/ispreg.h"
#include "ram.h"
#include "tools.h"

struct CV100_PERI_CRG12 {
    unsigned int sense_cksel : 3;
};

static char *cv100_sensor_clksel(unsigned int sensor_clksel) {
    switch (sensor_clksel) {
    case 0:
        return "12MHz";
    case 1:
        return "24MHz";
    case 2:
    case 5:
        return "27MHz";
    case 3:
        return "54MHz";
    case 4:
        return "13.54MHz";
    case 6:
        return "37.125MHz";
    case 7:
        return "74.25MHz";
    }
}

#define CV100_PERI_CRG12_ADDR 0x20030030
static void hisi_cv100_sensor_clock(cJSON *j_inner) {
    struct CV100_PERI_CRG12 crg12;
    if (mem_reg(CV100_PERI_CRG12_ADDR, (uint32_t *)&crg12, OP_READ)) {
        ADD_PARAM("clock", cv100_sensor_clksel(crg12.sense_cksel));
    }
}

static void hisi_cv100_sensor_data(cJSON *j_root) {
    cJSON *j_inner = cJSON_CreateObject();
    cJSON_AddItemToObject(j_root, "data", j_inner);
    ADD_PARAM("type", "DC");
}

enum CV200_MIPI_PHY {
    CV200_PHY_MIPI_MODE = 0,
    CV200_PHY_LVDS_MODE,
    CV200_PHY_CMOS_MODE,
    CV200_PHY_BYPASS
};

struct CV200_MISC_CTRL1 {
    unsigned int sdio0_card_det_mode : 1;
    unsigned int sdio1_card_det_mode : 1;
    unsigned int tde_ddrt_mst_sel : 1;
    bool bootram0_ck_gt_en : 1;
    bool bootram1_ck_gt_en : 1;
    unsigned int res0 : 1;
    unsigned int bootrom_pgen : 1;
    unsigned int uart1_rts_ctrl : 1;
    unsigned int spi0_cs0_ctrl : 1;
    unsigned int spi1_cs0_ctrl : 1;
    unsigned int spi1_cs1_ctrl : 1;
    unsigned int res1 : 1;
    unsigned int ive_gzip_mst_sel : 1;
    unsigned int res2 : 7;
    enum CV200_MIPI_PHY mipi_phy_mode : 2;
    unsigned int res3 : 4;
    unsigned int ssp1_cs_sel : 2;
    unsigned int res4 : 2;
    unsigned int vicap_vpss_online : 1;
};

#define CV200_MISC_CTRL1_ADDR 0x20120004
static void hisi_cv200_sensor_data(cJSON *j_root) {
    cJSON *j_inner = cJSON_CreateObject();

    struct CV200_MISC_CTRL1 ctrl1;
    bool res = mem_reg(CV200_MISC_CTRL1_ADDR, (uint32_t *)&ctrl1, OP_READ);
    if (res && *(uint32_t *)&ctrl1) {
        switch (ctrl1.mipi_phy_mode) {
        case CV200_PHY_MIPI_MODE:
            ADD_PARAM("type", "MIPI");
            break;
        case CV200_PHY_CMOS_MODE:
            ADD_PARAM("type", "DC");
            break;
        case CV200_PHY_LVDS_MODE:
            ADD_PARAM("type", "LVDS");
            break;
        case CV200_PHY_BYPASS:
            ADD_PARAM("type", "BYPASS");
            break;
        default:
            return;
        }
    }
    cJSON_AddItemToObject(j_root, "data", j_inner);
}

enum CV300_MIPI_PHY {
    CV300_PHY_MIPI_MODE = 0,
    CV300_PHY_LVDS_MODE,
    CV300_PHY_CMOS_MODE,
    CV300_PHY_RESERVED
};

struct CV300_MISC_CTRL0 {
    unsigned int res0 : 1;
    unsigned int spi0_cs0_pctrl : 1;
    unsigned int spi1_cs0_pctrl : 1;
    unsigned int spi1_cs1_pctrl : 1;
    unsigned int ssp1_cs_sel : 1;
    enum CV300_MIPI_PHY mipi_phy_mode : 3;
    bool vicap_vpss_online_mode : 1;
    unsigned int test_clksel : 4;
    unsigned int res1 : 2;
    bool commtx_rx_int_en : 1;
};

enum AV200_VICAP_INPUT_SEL {
    AV200_VICAP_INPUT_MIPI0 = 0,
    AV200_VICAP_INPUT_MIPI1,
    AV200_VICAP_INPUT_CMOS0,
    AV200_VICAP_INPUT_CMOS1,
};

struct AV200_MISC_CTRL0 {
    enum AV200_VICAP_INPUT_SEL vicap0_input_sel : 2;
    enum AV200_VICAP_INPUT_SEL vicap1_input_sel : 2;
    enum CV300_MIPI_PHY mipi1_work_mode : 2;
    enum CV300_MIPI_PHY mipi0_work_mode : 2;
};

struct CV200_PERI_CRG11 {
    bool vi0_cken : 1;
    unsigned int vi0_pctrl : 1;
    unsigned int vi0_sc_sel : 1;
    unsigned int res0 : 1;
    unsigned int phy_hs_lat : 2;
    unsigned int phy_cmos_lat : 2;
    unsigned int vi0_srst_req : 1;
    unsigned int vi_hrst_req : 1;
    unsigned int mipi_srst_req : 1;
    unsigned int res1 : 1;
    unsigned int isp_core_srst_req : 1;
    unsigned int isp_cfg_srst_req : 1;
    bool mipi_cken : 1;
    unsigned int res2 : 1;
    unsigned int sensor_cksel : 3;
    bool sensor_cken : 1;
    unsigned int sensor_srst_req : 1;
};

struct CV300_PERI_CRG11 {
    unsigned int vi0_cken : 1;
    unsigned int vi0_pctrl : 1;
    unsigned int vi0_sc_sel : 3;
    unsigned int res0 : 3;
    unsigned int vi0_srst_req : 1;
    unsigned int vi_hrst_req : 1;
    unsigned int vi_ch0_srst_req : 1;
    unsigned int mipi_srst_req : 1;
    unsigned int res1 : 1;
    unsigned int isp_core_srst_req : 1;
    unsigned int isp_cfg_srst_req : 1;
    unsigned int mipi_cken : 1;
    unsigned int mipi_core_srst_req : 1;
    unsigned int isp_clksel : 1;
    unsigned int isp_cken : 1;
    unsigned int phy_hs_lat : 2;
    unsigned int phy_cmos_lat : 2;
    unsigned int sensor_clksel : 3;
    bool sensor_cken : 1;
    unsigned int sensor_srst_req : 1;
    unsigned int res2 : 4;
};

#define CV300_MIPI_BASE 0x11300000
#define CV300_LVDS0_IMGSIZE_ADDR CV300_MIPI_BASE + 0x130C
struct LVDS0_IMGSIZE {
    unsigned int lvds_imgwidth_lane : 16;
    unsigned int lvds_imgheight : 16;
};

#define CV300_LVDS0_WDR_ADDR CV300_MIPI_BASE + 0x1300
struct CV300_LVDS0_WDR {
    bool lvds_wdr_en : 1;
    unsigned int res0 : 3;
    unsigned int lvds_wdr_num : 2;
    unsigned int res1 : 2;
    unsigned int lvds_wdr_mode : 4;
    unsigned int lvds_wdr_id_shift : 4;
};

typedef enum {
    RAW_UNKNOWN = 0,
    RAW_DATA_8BIT,
    RAW_DATA_10BIT,
    RAW_DATA_12BIT,
    RAW_DATA_14BIT,
    RAW_DATA_16BIT,
} raw_data_type_e;

typedef enum {
    LVDS_SYNC_MODE_SOF = 0, /* sensor SOL, EOL, SOF, EOF */
    LVDS_SYNC_MODE_SAV,     /* SAV, EAV */
} lvds_sync_mode_t;

typedef enum {
    LVDS_ENDIAN_LITTLE = 0x0,
    LVDS_ENDIAN_BIG = 0x1,
} lvds_bit_endian_t;

#define CV300_LVDS0_CTRL_ADDR CV300_MIPI_BASE + 0x1304
struct LVDS0_CTRL {
    lvds_sync_mode_t lvds_sync_mode : 1;
    unsigned int res0 : 3;
    raw_data_type_e lvds_raw_type : 3;
    unsigned int res1 : 1;
    lvds_bit_endian_t lvds_pix_big_endian : 1;
    lvds_bit_endian_t lvds_code_big_endian : 1;
    unsigned int res2 : 2;
    bool lvds_crop_en : 1;
    unsigned int res3 : 3;
    unsigned int lvds_split_mode : 3;
};

#define CV200_MIPI_LANES_NUM_ADDR 0x20680000 + 0x1030
struct CV200_MIPI_LANES_NUM {
    unsigned int lane_num : 2;
};

static size_t cv200_mipi_lanes_num() {
    struct CV200_MIPI_LANES_NUM lnum;
    mem_reg(CV200_MIPI_LANES_NUM_ADDR, (uint32_t *)&lnum, OP_READ);
    return lnum.lane_num + 1;
}

#define CV300_MIPI0_LANES_NUM_ADDR CV300_MIPI_BASE + 0x1004
struct CV300_MIPI0_LANES_NUM {
    unsigned int lane_num : 3;
};

static size_t cv300_mipi_lanes_num() {
    struct CV300_MIPI0_LANES_NUM lnum;
    mem_reg(CV300_MIPI0_LANES_NUM_ADDR, (uint32_t *)&lnum, OP_READ);
    return lnum.lane_num + 1;
}

#define EV200_MIPI_LANES_NUM_ADDR 0x11240000 + 0x1004
struct EV200_MIPI_LANES_NUM {
    unsigned int lane_num : 2;
};

static size_t ev200_mipi_lanes_num() {
    struct EV200_MIPI_LANES_NUM lnum;
    mem_reg(EV200_MIPI_LANES_NUM_ADDR, (uint32_t *)&lnum, OP_READ);
    return lnum.lane_num + 1;
}

#define EV300_MIPI_LANES_NUM_ADDR 0x11240000 + 0x1004
struct EV300_MIPI_LANES_NUM {
    unsigned int lane_num : 3;
};

static size_t ev300_mipi_lanes_num() {
    struct EV300_MIPI_LANES_NUM lnum;
    mem_reg(EV300_MIPI_LANES_NUM_ADDR, (uint32_t *)&lnum, OP_READ);
    return lnum.lane_num + 1;
}

static size_t mipi_lanes_num() {
    switch (chip_generation) {
    case HISI_V2A:
    case HISI_V2:
        return cv200_mipi_lanes_num();
    case HISI_V3:
        return cv300_mipi_lanes_num();
    case HISI_V4:
        if (!strcmp(chip_name, "3516EV200"))
            return ev200_mipi_lanes_num();
        else
            return ev300_mipi_lanes_num();
    }
    return 0;
}

#define CV200_MIPI_BASE 0x20680000
#define CV200_LANE_ID_LINK0_ADDR CV200_MIPI_BASE + 0x1014
struct CV200_LANE_ID_LINK0 {
    unsigned int lane0_id : 2;
    unsigned int res0 : 2;
    unsigned int lane1_id : 2;
    unsigned int res1 : 2;
    unsigned int lane2_id : 2;
    unsigned int res2 : 2;
    unsigned int lane3_id : 2;
    unsigned int res3 : 2;
};

#define CV300_ALIGN0_LANE_ID_ADDR CV300_MIPI_BASE + 0x1600
struct CV300_ALIGN0_LANE_ID {
    unsigned int lane0_id : 4;
    unsigned int lane1_id : 4;
    unsigned int lane2_id : 4;
    unsigned int lane3_id : 4;
};

#define EV300_MIPI_BASE 0x11240000
#define EV200_LANE_ID0_CHN_ADDR EV300_MIPI_BASE + 0x1800
struct EV200_LANE_ID0_CHN {
    unsigned int lane0_id : 4;
    unsigned int res0 : 4;
    unsigned int lane2_id : 4;
    unsigned int res1 : 4;
};

#define EV300_LANE_ID0_CHN_ADDR EV300_MIPI_BASE + 0x1800
struct EV300_LANE_ID0_CHN {
    unsigned int lane0_id : 4;
    unsigned int lane1_id : 4;
    unsigned int lane2_id : 4;
    unsigned int lane3_id : 4;
};

static void ev300_enum_lanes(cJSON *j_inner, size_t lanes) {
    if (!strcmp(chip_name, "3516EV200")) {
        struct EV200_LANE_ID0_CHN lid;
        if (mem_reg(EV200_LANE_ID0_CHN_ADDR, (uint32_t *)&lid, OP_READ)) {
            cJSON *j_lanes = cJSON_AddArrayToObject(j_inner, "lane-id");
            cJSON_AddItemToArray(j_lanes, cJSON_CreateNumber(lid.lane0_id));
            if (lanes > 1)
                cJSON_AddItemToArray(j_lanes, cJSON_CreateNumber(lid.lane2_id));
        }

    } else {
        struct EV300_LANE_ID0_CHN lid;
        if (mem_reg(EV300_LANE_ID0_CHN_ADDR, (uint32_t *)&lid, OP_READ)) {
            cJSON *j_lanes = cJSON_AddArrayToObject(j_inner, "lane-id");
            cJSON_AddItemToArray(j_lanes, cJSON_CreateNumber(lid.lane0_id));
            if (lanes > 1)
                cJSON_AddItemToArray(j_lanes, cJSON_CreateNumber(lid.lane1_id));
            if (lanes > 2)
                cJSON_AddItemToArray(j_lanes, cJSON_CreateNumber(lid.lane2_id));
            if (lanes > 3)
                cJSON_AddItemToArray(j_lanes, cJSON_CreateNumber(lid.lane3_id));
        }
    }
}

static void lvds_code_set(cJSON *j_inner, const char *param,
                          lvds_bit_endian_t val) {
    if (val == LVDS_ENDIAN_LITTLE)
        ADD_PARAM(param, "LVDS_ENDIAN_LITTLE");
    else
        ADD_PARAM(param, "LVDS_ENDIAN_BIG");
}

#define LO(x) x & 0xffff
#define HI(x) x & 0xffff0000 >> 16

static void cv300_enum_sync_codes(cJSON *j_inner) {
    uint32_t addr = 0x11301320;
    uint32_t end_addr = 0x1130141C;

    cJSON *j_codes = cJSON_AddArrayToObject(j_inner, "sync-code");

    for (int l = 0; l < 4; l++) {
        char sync_code[4][64] = {0};

        uint32_t reg[8];
        for (int r = 0; r < 8; r++)
            mem_reg(addr + 4 * r, &reg[r], OP_READ);
        addr += 4 * 8;

        snprintf(sync_code[0], 64, "0x%x, 0x%x, 0x%x, 0x%x", LO(reg[0]),
                 LO(reg[2]), LO(reg[4]), LO(reg[6]));
        snprintf(sync_code[1], 64, "0x%x, 0x%x, 0x%x, 0x%x", HI(reg[0]),
                 HI(reg[2]), HI(reg[4]), HI(reg[6]));
        snprintf(sync_code[2], 64, "0x%x, 0x%x, 0x%x, 0x%x", LO(reg[1]),
                 LO(reg[3]), LO(reg[4]), LO(reg[6]));
        snprintf(sync_code[3], 64, "0x%x, 0x%x, 0x%x, 0x%x", HI(reg[1]),
                 HI(reg[3]), HI(reg[4]), HI(reg[6]));

        cJSON *j_lane = cJSON_CreateArray();
        cJSON_AddItemToArray(j_lane, cJSON_CreateString(sync_code[0]));
        cJSON_AddItemToArray(j_lane, cJSON_CreateString(sync_code[1]));
        cJSON_AddItemToArray(j_lane, cJSON_CreateString(sync_code[2]));
        cJSON_AddItemToArray(j_lane, cJSON_CreateString(sync_code[3]));
        cJSON_AddItemToArray(j_codes, j_lane);
    }
}

static void vicap_input_set(cJSON *j_inner, const char *param,
                            enum AV200_VICAP_INPUT_SEL val) {
    switch (val) {
    case AV200_VICAP_INPUT_MIPI0:
        ADD_PARAM(param, "MIPI0");
        break;
    case AV200_VICAP_INPUT_MIPI1:
        ADD_PARAM(param, "MIPI1");
        break;
    case AV200_VICAP_INPUT_CMOS0:
        ADD_PARAM(param, "CMOS0");
        break;
    case AV200_VICAP_INPUT_CMOS1:
        ADD_PARAM(param, "CMOS1");
        break;
    }
}

static const char *cv300_mipi_phy(enum CV300_MIPI_PHY val) {
    switch (val) {
    case CV300_PHY_CMOS_MODE:
        return "DC";
    case CV300_PHY_LVDS_MODE:
        return "LVDS";
    case CV300_PHY_MIPI_MODE:
        return "MIPI";
    default:
        return NULL;
    }
}

#define AV200_MISC_CTRL0_ADDR 0x12030000
static void hisi_av200_sensor_data(cJSON *j_root, int vistate) {
    struct AV200_MISC_CTRL0 ctrl0;
    if (!mem_reg(AV200_MISC_CTRL0_ADDR, (uint32_t *)&ctrl0, OP_READ))
        return;

    cJSON *j_inner = cJSON_CreateObject();

    if (vistate & 1) {
        vicap_input_set(j_inner, "vicap0-input", ctrl0.vicap0_input_sel);
        ADD_PARAM("mipi0-type", cv300_mipi_phy(ctrl0.mipi0_work_mode));
    }
    if (vistate & 2) {
        vicap_input_set(j_inner, "vicap1-input", ctrl0.vicap1_input_sel);
        ADD_PARAM("mipi1-type", cv300_mipi_phy(ctrl0.mipi1_work_mode));
    }

    cJSON_AddItemToObject(j_root, "data", j_inner);
}

#define CV300_MISC_CTRL0_ADDR 0x12030000
static void hisi_cv300_sensor_data(cJSON *j_root) {
    struct CV300_MISC_CTRL0 ctrl0;
    if (!mem_reg(CV300_MISC_CTRL0_ADDR, (uint32_t *)&ctrl0, OP_READ))
        return;

    cJSON *j_inner = cJSON_CreateObject();
    ADD_PARAM("type", cv300_mipi_phy(ctrl0.mipi_phy_mode));

    if (ctrl0.mipi_phy_mode == CV300_PHY_LVDS_MODE ||
        ctrl0.mipi_phy_mode == CV300_PHY_MIPI_MODE) {
        size_t lanes = mipi_lanes_num();

        struct CV300_ALIGN0_LANE_ID lid;
        if (mem_reg(CV300_ALIGN0_LANE_ID_ADDR, (uint32_t *)&lid, OP_READ)) {
            cJSON *j_lanes = cJSON_AddArrayToObject(j_inner, "lane-id");
            cJSON_AddItemToArray(j_lanes, cJSON_CreateNumber(lid.lane0_id));
            if (lanes > 1)
                cJSON_AddItemToArray(j_lanes, cJSON_CreateNumber(lid.lane1_id));
            if (lanes > 2)
                cJSON_AddItemToArray(j_lanes, cJSON_CreateNumber(lid.lane2_id));
            if (lanes > 3)
                cJSON_AddItemToArray(j_lanes, cJSON_CreateNumber(lid.lane3_id));
        }
    }

    if (ctrl0.mipi_phy_mode == CV300_PHY_LVDS_MODE) {
        struct CV300_LVDS0_WDR wdr;
        if (mem_reg(CV300_LVDS0_WDR_ADDR, (uint32_t *)&wdr, OP_READ)) {
            ADD_PARAM_NUM("lvds-wdr-en", wdr.lvds_wdr_en);
            ADD_PARAM_NUM("lvds-wdr-mode", wdr.lvds_wdr_mode);
            ADD_PARAM_NUM("lvds-wdr-num", wdr.lvds_wdr_num);
        }

        struct LVDS0_CTRL lvds0_ctrl;
        if (mem_reg(CV300_LVDS0_CTRL_ADDR, (uint32_t *)&lvds0_ctrl, OP_READ)) {

            const char *raw;
            switch (lvds0_ctrl.lvds_raw_type) {
            case RAW_DATA_8BIT:
                raw = "RAW_DATA_8BIT";
                break;
            case RAW_DATA_10BIT:
                raw = "RAW_DATA_10BIT";
                break;
            case RAW_DATA_12BIT:
                raw = "RAW_DATA_12BIT";
                break;
            case RAW_DATA_14BIT:
                raw = "RAW_DATA_14BIT";
                break;
            case RAW_DATA_16BIT:
                raw = "RAW_DATA_16BIT";
                break;
            default:
                raw = NULL;
            }
            if (raw)
                ADD_PARAM("raw-data-type", raw);
        }
        if (lvds0_ctrl.lvds_sync_mode == LVDS_SYNC_MODE_SOF)
            ADD_PARAM("sync-mode", "LVDS_SYNC_MODE_SOF");
        else
            ADD_PARAM("sync-mode", "LVDS_SYNC_MODE_SAV");

        lvds_code_set(j_inner, "data-endian", lvds0_ctrl.lvds_pix_big_endian);
        lvds_code_set(j_inner, "sync-code-endian",
                      lvds0_ctrl.lvds_code_big_endian);
        cv300_enum_sync_codes(j_inner);
    }

    cJSON_AddItemToObject(j_root, "data", j_inner);
}

static char *cv200_cv300_map_sensor_clksel(unsigned int sensor_clksel) {
    switch (sensor_clksel) {
    case 0:
        return "74.25MHz";
    case 1:
        return "37.125MHz";
    case 2:
        return "54MHz";
    case 3:
        return "27MHz";
    default:
        if (sensor_clksel & 1) {
            return "25MHz";
        } else {
            return "24MHz";
        }
    }
}

#define CV200_PERI_CRG11_ADDR 0x2003002c
static void hisi_cv200_sensor_clock(cJSON *j_inner) {
    struct CV200_PERI_CRG11 crg11;
    int res = mem_reg(CV200_PERI_CRG11_ADDR, (uint32_t *)&crg11, OP_READ);
    // consider sensor clock value only when it's enabled
    if (res && crg11.sensor_cken) {
        ADD_PARAM("clock", cv200_cv300_map_sensor_clksel(crg11.sensor_cksel));
    }
}

static void hisi_cv300_sensor_clock(cJSON *j_inner) {
    struct CV300_PERI_CRG11 crg11;
    int res = mem_reg(CV300_PERI_CRG11_ADDR, (uint32_t *)&crg11, OP_READ);
    // consider sensor clock value only when it's enabled
    if (res && crg11.sensor_cken) {
        ADD_PARAM("clock", cv200_cv300_map_sensor_clksel(crg11.sensor_clksel));
    }
}

enum EV300_MIPI_PHY {
    EV300_PHY_MIPI_MODE = 0,
    EV300_PHY_LVDS_MODE,
    EV300_PHY_RESERVED0,
    EV300_PHY_RESERVED1
};

#define EV300_MISC_CTRL6_ADDR 0x12028018
struct EV300_MISC_CTRL6 {
    enum EV300_MIPI_PHY mipirx0_work_mode : 2;
};

#define EV300_MIPI_IMGSIZE EV300_MIPI_BASE + 0x1224
struct CV300_EV300_MIPI_IMGSIZE {
    unsigned int mipi_imgwidth : 16;
    unsigned int mipi_imgheight : 16;
};

#define EV300_MIPI_DI_1_ADDR EV300_MIPI_BASE + 0x1010
struct EV300_MIPI_DI_1 {
    unsigned int di0_dt : 6;
    unsigned int di0_vc : 2;
    unsigned int di1_dt : 6;
    unsigned int di1_vc : 2;
    unsigned int di2_dt : 6;
    unsigned int di2_vc : 2;
    unsigned int di3_dt : 6;
    unsigned int di3_vc : 2;
};

static const char *ev300_mipi_raw_data(unsigned int di0_dt) {
    switch (di0_dt) {
    case 0x2A:
        return "DATA_TYPE_RAW_8BIT";
    case 0x2B:
        return "DATA_TYPE_RAW_10BIT";
    case 0x2C:
        return "DATA_TYPE_RAW_12BIT";
    case 0x2D:
        return "DATA_TYPE_RAW_14BIT";
    default:
        return NULL;
    }
}

static void hisi_ev300_sensor_data(cJSON *j_root) {
    cJSON *j_inner = cJSON_CreateObject();

    struct EV300_MISC_CTRL6 ctrl6;
    bool res = mem_reg(EV300_MISC_CTRL6_ADDR, (uint32_t *)&ctrl6, OP_READ);
    if (res) {
        switch (ctrl6.mipirx0_work_mode) {
        case EV300_PHY_MIPI_MODE:
            ADD_PARAM("type", "MIPI");

            struct EV300_MIPI_DI_1 di1;
            mem_reg(EV300_MIPI_DI_1_ADDR, (uint32_t *)&di1, OP_READ);
            ADD_PARAM("input-data-type", ev300_mipi_raw_data(di1.di0_dt));

            // MIPI_CTRL_MODE_HS
            // vc_mode
            // hdr_mode

            size_t lanes = mipi_lanes_num();
            ev300_enum_lanes(j_inner, lanes);

            struct CV300_EV300_MIPI_IMGSIZE size;
            mem_reg(EV300_MIPI_IMGSIZE, (uint32_t *)&size, OP_READ);
            ADD_PARAM_FMT("image", "%dx%d", size.mipi_imgwidth + 1,
                          size.mipi_imgheight + 1);
            break;
        case EV300_PHY_LVDS_MODE:
            ADD_PARAM("type", "LVDS");
            break;
        default:
            return;
        }
    }
    cJSON_AddItemToObject(j_root, "data", j_inner);
}

static const char *ev300_map_sensor_clksel(unsigned int sensor0_cksel) {
    switch (sensor0_cksel) {
    case 0:
        return "74.25MHz";
    case 1:
        return "72MHz";
    case 2:
        return "54MHz";
    case 3:
        return "24MHz";
    case 4:
        return "37.125MHz";
    case 5:
        return "36MHz";
    case 6:
        return "27MHz";
    case 7:
        return "12MHz";
    default:
        return NULL;
    }
}

static void hisi_ev300_sensor_clock(cJSON *j_inner) {
    struct EV300_PERI_CRG60 crg60;
    int res = mem_reg(EV300_PERI_CRG60_ADDR, (uint32_t *)&crg60, OP_READ);
    if (res && crg60.sensor0_cken) {
        ADD_PARAM("clock", ev300_map_sensor_clksel(crg60.sensor0_cksel));
    }
}

bool hisi_ev300_get_die_id(char *buf, ssize_t len) {
    uint32_t base_id_addr = 0x12020400;
    char *ptr = buf;
    for (uint32_t id_addr = base_id_addr + 5 * 4; id_addr >= base_id_addr;
         id_addr -= 4) {
        uint32_t val;
        if (!mem_reg(id_addr, &val, OP_READ))
            return false;
        int outsz = snprintf(ptr, len, "%08x", val);
        ptr += outsz;
        len -= outsz;
    }

    // remove trailing zeroes
    int nlen = strlen(buf);
    for (int i = nlen; i > 0; i--) {
        if (buf[i - 1] != '0') {
            buf[i] = 0;
            break;
        }
    }

    return true;
}

#define CV300_ISP_AF_CFG_ADDR 0x12200
struct CV300_ISP_AF_CFG {
    bool en : 1;
    bool iir0_en0 : 1;
    bool iir0_en1 : 1;
    bool iir0_en2 : 1;
    bool iir1_en0 : 1;
    bool iir1_en1 : 1;
    bool iir1_en2 : 1;
    unsigned int peak_mode : 1;
    unsigned int squ_mode : 1;
    bool offset_en : 1;
    bool crop_en : 1;
    bool lpf_en : 1;
    bool mean_en : 1;
    bool sqrt_en : 1;
    bool raw_mode : 1;
    unsigned int bayer_mode : 2;
    bool iir0_ds_en : 1;
    bool iir1_ds_en : 1;
    bool fir0_lpf_en : 1;
    bool fir1_lpf_en : 1;
    bool iir0_ldg_en : 1;
    bool iir1_ldg_en : 1;
    bool fir0_ldg_en : 1;
    bool fir1_ldg_en : 1;
    unsigned int res : 6;
    bool ck_gt_en : 1;
};

enum CV300_ISP_AF_BAYER {
    CV300_B_RGGB = 0,
    CV300_B_GRBG,
    CV300_B_GBRG,
    CV300_B_BGGR,
};

struct PT_INTF_MOD {
    unsigned int mode : 1;
    unsigned int res : 30;
    bool enable : 1;
};

// cv100 - 0x0110
// cv200 - 0x0110
// cv300 - 0x0110
// cv500 - 0x1000 + PT_N x 0x100
// ev300 - 0x1000 + PT_N x 0x100

int hisi_vi_is_running(cJSON *j_inner) {
    uint32_t vicap0 = 0, vicap1 = 0, PT_N = 0;
    switch (chip_generation) {
    case HISI_V1:
    case HISI_V2A:
    case HISI_V2:
        vicap0 = 0x20580000 + 0x0100;
        break;
    case HISI_V3A:
        vicap0 = 0x11380000 + 0x0100;
        vicap1 = 0x11480000 + 0x0100;
        break;
    case HISI_V3:
        vicap0 = 0x11380000 + 0x0100;
        break;
    case HISI_V4A:
        vicap0 = 0x11300000 + 0x1000 + PT_N * 0x100;
        break;
    case HISI_V4:
        vicap0 = 0x11000000 + 0x1000 + PT_N * 0x100;
        break;
    default:
        return false;
    }
    struct PT_INTF_MOD reg1, reg2;
    if (!mem_reg(vicap0, (uint32_t *)&reg1, OP_READ))
        return 0;
    if (vicap1 == 0) {
        if (!reg1.enable)
            ADD_PARAM("vicap-state", "down");
        return reg1.enable;
    }
    ADD_PARAM("vicap0-state", reg1.enable ? "up" : "down");

    if (!mem_reg(vicap1, (uint32_t *)&reg2, OP_READ))
        return 0;
    ADD_PARAM("vicap1-state", reg2.enable ? "up" : "down");
    return reg1.enable | (reg2.enable << 1);
}

static void determine_sensor_data_type(cJSON *j_inner, int vistate) {
    switch (chip_generation) {
    case HISI_V1:
        return hisi_cv100_sensor_data(j_inner);
    case HISI_V2A:
    case HISI_V2:
        return hisi_cv200_sensor_data(j_inner);
    case HISI_V3A:
        return hisi_av200_sensor_data(j_inner, vistate);
    case HISI_V3:
        return hisi_cv300_sensor_data(j_inner);
    case HISI_V4:
        return hisi_ev300_sensor_data(j_inner);
    default:
        return;
    }
}

static void determine_sensor_clock(cJSON *j_inner) {
    switch (chip_generation) {
    case HISI_V1:
        return hisi_cv100_sensor_clock(j_inner);
    case HISI_V2A:
    case HISI_V2:
        return hisi_cv200_sensor_clock(j_inner);
    case HISI_V3:
        return hisi_cv300_sensor_clock(j_inner);
    case HISI_V4:
        return hisi_ev300_sensor_clock(j_inner);
    }
}

void hisi_vi_information(sensor_ctx_t *ctx) {
    int vistate = hisi_vi_is_running(ctx->j_sensor);
    if (vistate == 0)
        return;

    determine_sensor_data_type(ctx->j_sensor, vistate);
    determine_sensor_clock(ctx->j_sensor);
}

#define CV100_FMC_BASE 0x10010000

#define CV100_GLOBAL_CONFIG_ADDR 0x0100
struct CV100_GLOBAL_CONFIG {
    unsigned int mode : 1;
    bool wp_en : 1;
    unsigned int flash_addr_mode : 1;
};

#define CV200_FMC_BASE 0x10010000
#define CV300_FMC_BASE 0x10000000
#define EV300_FMC_BASE 0x10000000

#define CV200_FMC_CFG CV200_FMC_BASE + 0
#define CV300_FMC_CFG CV300_FMC_BASE + 0
#define EV300_FMC_CFG EV300_FMC_BASE + 0
struct FMC_CFG {
    unsigned int op_mode : 1;
    unsigned int flash_sel : 2;
    unsigned int page_size : 2;
    unsigned int ecc_type : 3;
    unsigned int block_size : 2;
    unsigned int spi_nor_addr_mode : 1;
    unsigned int spi_nand_sel : 2;
};

static const char *hisi_flash_mode(unsigned int value) {
    switch (value) {
    case 0:
        return "3-byte";
    case 1:
        return "4-byte";
    default:
        return NULL;
    }
}

static const char *hisi_fmc_mode(uint32_t addr) {
    struct FMC_CFG val;
    if (mem_reg(addr, (uint32_t *)&val, OP_READ))
        return (hisi_flash_mode(val.spi_nor_addr_mode));
    return NULL;
}

const char *hisi_detect_fmc() {
    const char *mode = NULL;
    switch (chip_generation) {
    case HISI_V1: {
        struct CV100_GLOBAL_CONFIG val;
        if (mem_reg(CV100_GLOBAL_CONFIG_ADDR, (uint32_t *)&val, OP_READ))
            mode = hisi_flash_mode(val.flash_addr_mode);
    } break;
    case HISI_V2:
    case HISI_V2A:
        mode = hisi_fmc_mode(CV200_FMC_CFG);
        break;
    case HISI_V3:
        mode = hisi_fmc_mode(CV300_FMC_CFG);
        break;
    case HISI_V4:
        mode = hisi_fmc_mode(EV300_FMC_CFG);
        break;
    }
    return mode;
}

// for IMX291 1920x1110
struct PT_SIZE {
    unsigned int width : 16;
    unsigned int height : 16;
};

struct PT_OFFSET {
    unsigned int offset : 6;
    unsigned int res : 9;
    bool rev : 1;
    unsigned int mask : 16;
};

// PT_UNIFY_TIMING_CFG

void hisi_chip_properties(cJSON *j_inner) {
    if (chip_generation == HISI_V4) {
        char buf[1024];
        if (hisi_ev300_get_die_id(buf, sizeof buf)) {
            ADD_PARAM("id", buf);
        }
    }
}
