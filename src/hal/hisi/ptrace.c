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
#include "hal/hisi/ptrace.h"
#include "ram.h"
#include "tools.h"

typedef unsigned int combo_dev_t;

// for V2 platform use +1 to get actual values
typedef enum {
    RAW_DATA_8BIT = 0,
    RAW_DATA_10BIT,
    RAW_DATA_12BIT,
    RAW_DATA_14BIT,
    RAW_DATA_16BIT,
} raw_data_type_e;

const char *raw_data_type_str(raw_data_type_e val) {
    switch (val) {
    case RAW_DATA_8BIT:
        return "RAW_DATA_8BIT";
    case RAW_DATA_10BIT:
        return "RAW_DATA_10BIT";
    case RAW_DATA_12BIT:
        return "RAW_DATA_12BIT";
    case RAW_DATA_14BIT:
        return "RAW_DATA_14BIT";
    case RAW_DATA_16BIT:
        return "RAW_DATA_16BIT";
    }
}

typedef enum {
    LVDS_SYNC_MODE_SOF = 0,
    LVDS_SYNC_MODE_SAV,
} lvds_sync_mode_t;

const char *sync_mode_str(lvds_sync_mode_t val) {
    switch (val) {
    case LVDS_SYNC_MODE_SOF:
        return "LVDS_SYNC_MODE_SOF";
    case LVDS_SYNC_MODE_SAV:
        return "LVDS_SYNC_MODE_SAV";
    }
    return NULL;
}

typedef enum {
    LVDS_ENDIAN_LITTLE = 0x0,
    LVDS_ENDIAN_BIG = 0x1,
} lvds_bit_endian_t;

const char *data_endian_str(lvds_bit_endian_t val) {
    switch (val) {
    case LVDS_ENDIAN_LITTLE:
        return "LVDS_ENDIAN_LITTLE";
    case LVDS_ENDIAN_BIG:
        return "LVDS_ENDIAN_BIG";
    }
    return NULL;
}
#define sync_code_endian_str data_endian_str

typedef enum {
    INPUT_MODE_MIPI = 0x0,
    INPUT_MODE_SUBLVDS = 0x1,
    INPUT_MODE_LVDS = 0x2,
    INPUT_MODE_HISPI = 0x3,
    INPUT_MODE_CMOS = 0x4,
    INPUT_MODE_BT601 = 0x5,
    INPUT_MODE_BT656 = 0x6,
    INPUT_MODE_BT1120 = 0x7,
    INPUT_MODE_BYPASS = 0x8,
} input_mode_t;

const char *input_mode_str(input_mode_t val) {
    switch (val) {
    case INPUT_MODE_MIPI:
        return "INPUT_MODE_MIPI";
    case INPUT_MODE_SUBLVDS:
        return "INPUT_MODE_SUBLVDS";
    case INPUT_MODE_LVDS:
        return "INPUT_MODE_LVDS";
    case INPUT_MODE_HISPI:
        return "INPUT_MODE_HISPI";
    case INPUT_MODE_CMOS:
        return "INPUT_MODE_CMOS";
    case INPUT_MODE_BT601:
        return "INPUT_MODE_BT601";
    case INPUT_MODE_BT656:
        return "INPUT_MODE_BT656";
    case INPUT_MODE_BT1120:
        return "INPUT_MODE_BT1120";
    case INPUT_MODE_BYPASS:
        return "INPUT_MODE_BYPASS";
    }
    return NULL;
}

typedef enum {
    MIPI_DATA_RATE_X1 = 0,
    MIPI_DATA_RATE_X2 = 1,
} mipi_data_rate_t;

const char *data_rate_str(mipi_data_rate_t val) {
    switch (val) {
    case MIPI_DATA_RATE_X1:
        return "MIPI_DATA_RATE_X1";
    case MIPI_DATA_RATE_X2:
        return "MIPI_DATA_RATE_X2";
    }
    return NULL;
}

typedef struct {
    int x;
    int y;
    unsigned int width;
    unsigned int height;
} img_rect_t;

typedef enum {
    DATA_TYPE_RAW_8BIT = 0,
    DATA_TYPE_RAW_10BIT,
    DATA_TYPE_RAW_12BIT,
    DATA_TYPE_RAW_14BIT,
    DATA_TYPE_RAW_16BIT,
    DATA_TYPE_YUV420_8BIT_NORMAL,
    DATA_TYPE_YUV420_8BIT_LEGACY,
    DATA_TYPE_YUV422_8BIT,
} data_type_t;

const char *input_data_type_str(data_type_t val) {
    switch (val) {
    case DATA_TYPE_RAW_8BIT:
        return "DATA_TYPE_RAW_8BIT";
    case DATA_TYPE_RAW_10BIT:
        return "DATA_TYPE_RAW_10BIT";
    case DATA_TYPE_RAW_12BIT:
        return "DATA_TYPE_RAW_12BIT";
    case DATA_TYPE_RAW_14BIT:
        return "DATA_TYPE_RAW_14BIT";
    case DATA_TYPE_RAW_16BIT:
        return "DATA_TYPE_RAW_16BIT";
    case DATA_TYPE_YUV420_8BIT_NORMAL:
        return "DATA_TYPE_YUV420_8BIT_NORMAL";
    case DATA_TYPE_YUV420_8BIT_LEGACY:
        return "DATA_TYPE_YUV420_8BIT_LEGACY";
    case DATA_TYPE_YUV422_8BIT:
        return "DATA_TYPE_YUV422_8BIT";
    }
    return NULL;
}

typedef enum {
    HI_MIPI_WDR_MODE_NONE = 0x0,
    HI_MIPI_WDR_MODE_VC = 0x1,
    HI_MIPI_WDR_MODE_DT = 0x2,
    HI_MIPI_WDR_MODE_DOL = 0x3,
} mipi_wdr_mode_t;

const char *mipi_wdr_mode_str(mipi_wdr_mode_t val) {
    switch (val) {
    case HI_MIPI_WDR_MODE_NONE:
        return "HI_MIPI_WDR_MODE_NONE";
    case HI_MIPI_WDR_MODE_VC:
        return "HI_MIPI_WDR_MODE_VC";
    case HI_MIPI_WDR_MODE_DT:
        return "HI_MIPI_WDR_MODE_DT";
    case HI_MIPI_WDR_MODE_DOL:
        return "HI_MIPI_WDR_MODE_DOL";
    }
    return NULL;
}

typedef enum {
    PHY_CLK_SHARE_NONE = 0x0,
    PHY_CLK_SHARE_PHY0 = 0x1,
} phy_clk_share_e;

const char *phy_clk_share_str(phy_clk_share_e val) {
    switch (val) {
    case PHY_CLK_SHARE_NONE:
        return "PHY_CLK_SHARE_NONE";
    case PHY_CLK_SHARE_PHY0:
        return "PHY_CLK_SHARE_PHY0";
    }
    return NULL;
}

#define V2A23A_MIPI_LANE_NUM 8
#define MIPI_LANE_NUM 4

#define V2_LVDS_LANE_NUM 8
#define V3A_LVDS_LANE_NUM 12
#define LVDS_LANE_NUM 4

#define WDR_VC_NUM 4
#define V4_WDR_VC_NUM 2

#define SYNC_CODE_NUM 4

typedef enum {
    HI_WDR_MODE_NONE = 0x0,
    HI_WDR_MODE_2F = 0x1,
    HI_WDR_MODE_3F = 0x2,
    HI_WDR_MODE_4F = 0x3,
    HI_WDR_MODE_DOL_2F = 0x4,
    HI_WDR_MODE_DOL_3F = 0x5,
    HI_WDR_MODE_DOL_4F = 0x6,
} wdr_mode_t;

static const char *wdr_mode_str(wdr_mode_t val) {
    switch (val) {
    case HI_WDR_MODE_NONE:
        return "HI_WDR_MODE_NONE";
    case HI_WDR_MODE_2F:
        return "HI_WDR_MODE_2F";
    case HI_WDR_MODE_3F:
        return "HI_WDR_MODE_3F";
    case HI_WDR_MODE_4F:
        return "HI_WDR_MODE_4F";
    case HI_WDR_MODE_DOL_2F:
        return "HI_WDR_MODE_DOL_2F";
    case HI_WDR_MODE_DOL_3F:
        return "HI_WDR_MODE_DOL_3F";
    case HI_WDR_MODE_DOL_4F:
        return "HI_WDR_MODE_DOL_4F";
    }
    return NULL;
}

typedef enum {
    LVDS_VSYNC_NORMAL = 0x00,
    LVDS_VSYNC_SHARE = 0x01,
    LVDS_VSYNC_HCONNECT = 0x02,
} lvds_vsync_type_t;

const char *sync_type_str(lvds_vsync_type_t val) {
    switch (val) {
    case LVDS_VSYNC_NORMAL:
        return "LVDS_VSYNC_NORMAL";
    case LVDS_VSYNC_SHARE:
        return "LVDS_VSYNC_SHARE";
    case LVDS_VSYNC_HCONNECT:
        return "LVDS_VSYNC_HCONNECT";
    }
    return NULL;
}

typedef struct {
    lvds_vsync_type_t sync_type;
    unsigned short hblank1;
    unsigned short hblank2;
} lvds_vsync_attr_t;

typedef struct {
    raw_data_type_e raw_data_type;
    short lane_id[V2A23A_MIPI_LANE_NUM];
} V2_mipi_dev_attr_t;

typedef struct {
    raw_data_type_e raw_data_type;
    mipi_wdr_mode_t wdr_mode;
    short lane_id[V2A23A_MIPI_LANE_NUM];
    union {
        short data_type[WDR_VC_NUM];
    };
} V3A_mipi_dev_attr_t;

typedef struct {
    raw_data_type_e raw_data_type;
    mipi_wdr_mode_t wdr_mode;
    short lane_id[MIPI_LANE_NUM];
    union {
        short data_type[WDR_VC_NUM];
    };
} V3_mipi_dev_attr_t;

typedef struct {
    data_type_t input_data_type;
    mipi_wdr_mode_t wdr_mode;
    short lane_id[MIPI_LANE_NUM];
    union {
        short data_type[WDR_VC_NUM];
    };
} V4A_mipi_dev_attr_t;

typedef struct {
    data_type_t input_data_type;
    mipi_wdr_mode_t wdr_mode;
    short lane_id[MIPI_LANE_NUM];
    union {
        short data_type[V4_WDR_VC_NUM];
    };
} V4_mipi_dev_attr_t;

typedef enum {
    LVDS_FID_NONE = 0x00,
    LVDS_FID_IN_SAV = 0x01,
    LVDS_FID_IN_DATA = 0x02,
} lvds_fid_type_t;

const char *fid_str(lvds_fid_type_t val) {
    switch (val) {
    case LVDS_FID_NONE:
        return "LVDS_FID_NONE";
    case LVDS_FID_IN_SAV:
        return "LVDS_FID_IN_SAV";
    case LVDS_FID_IN_DATA:
        return "LVDS_FID_IN_DATA";
    }
    return NULL;
}

static const char *bool_str(bool val) { return val ? "HI_TRUE" : "HI_FALSE"; }

typedef struct {
    lvds_fid_type_t fid_type;
    unsigned char output_fil;
} lvds_fid_attr_t;

typedef struct {
    unsigned int width;
    unsigned int height;
} img_size_t;

typedef struct {
    img_size_t img_size;
    wdr_mode_t wdr_mode;
    lvds_sync_mode_t sync_mode;
    raw_data_type_e raw_data_type;
    lvds_bit_endian_t data_endian;
    lvds_bit_endian_t sync_code_endian;
    short lane_id[V2_LVDS_LANE_NUM];
    unsigned short sync_code[V2_LVDS_LANE_NUM][WDR_VC_NUM][SYNC_CODE_NUM];
} V2_lvds_dev_attr_t;

typedef struct {
    data_type_t input_data_type;
    wdr_mode_t wdr_mode;
    lvds_sync_mode_t sync_mode;
    lvds_vsync_attr_t vsync_attr;
    lvds_fid_attr_t fid_attr;
    lvds_bit_endian_t data_endian;
    lvds_bit_endian_t sync_code_endian;
    short lane_id[V3A_LVDS_LANE_NUM];
    unsigned short sync_code[V3A_LVDS_LANE_NUM][WDR_VC_NUM][SYNC_CODE_NUM];
} V3A_lvds_dev_attr_t;

typedef struct {
    img_size_t img_size;
    raw_data_type_e raw_data_type;
    wdr_mode_t wdr_mode;
    lvds_sync_mode_t sync_mode;
    lvds_vsync_attr_t vsync_type;
    lvds_fid_attr_t fid_type;
    lvds_bit_endian_t data_endian;
    lvds_bit_endian_t sync_code_endian;
    short lane_id[LVDS_LANE_NUM];
    unsigned short sync_code[LVDS_LANE_NUM][WDR_VC_NUM][SYNC_CODE_NUM];
} V3_lvds_dev_attr_t;

typedef struct {
    data_type_t input_data_type;
    wdr_mode_t wdr_mode;
    lvds_sync_mode_t sync_mode;
    lvds_vsync_attr_t vsync_attr;
    lvds_fid_attr_t fid_attr;
    lvds_bit_endian_t data_endian;
    lvds_bit_endian_t sync_code_endian;
    short lane_id[LVDS_LANE_NUM];
    unsigned short sync_code[LVDS_LANE_NUM][WDR_VC_NUM][SYNC_CODE_NUM];
} V4A_lvds_dev_attr_t;

typedef struct {
    input_mode_t input_mode;
    union {
        V2_mipi_dev_attr_t mipi_attr;
        V2_lvds_dev_attr_t lvds_attr;
    };
} V2_combo_dev_attr_t;

typedef struct {
    combo_dev_t devno;
    input_mode_t input_mode;
    phy_clk_share_e phy_clk_share;
    img_rect_t img_rect;
    union {
        V3A_mipi_dev_attr_t mipi_attr;
        V3A_lvds_dev_attr_t lvds_attr;
    };
} V3A_combo_dev_attr_t;

typedef struct {
    combo_dev_t devno;
    input_mode_t input_mode;
    union {
        V3_mipi_dev_attr_t mipi_attr;
        V3_lvds_dev_attr_t lvds_attr;
    };
} V3_combo_dev_attr_t;

typedef struct {
    data_type_t input_data_type;
    wdr_mode_t wdr_mode;
    lvds_sync_mode_t sync_mode;
    lvds_vsync_attr_t vsync_attr;
    lvds_fid_attr_t fid_attr;
    lvds_bit_endian_t data_endian;
    lvds_bit_endian_t sync_code_endian;
    short lane_id[LVDS_LANE_NUM];
    unsigned short sync_code[LVDS_LANE_NUM][V4_WDR_VC_NUM][SYNC_CODE_NUM];
} V4_lvds_dev_attr_t;

typedef struct {
    combo_dev_t devno;
    input_mode_t input_mode;
    mipi_data_rate_t data_rate;
    img_rect_t img_rect;

    union {
        V4A_mipi_dev_attr_t mipi_attr;
        V4A_lvds_dev_attr_t lvds_attr;
    };
} V4A_combo_dev_attr_t;

typedef struct {
    combo_dev_t devno;
    input_mode_t input_mode;
    mipi_data_rate_t data_rate;
    img_rect_t img_rect;

    union {
        V4_mipi_dev_attr_t mipi_attr;
        V4_lvds_dev_attr_t lvds_attr;
    };
} V4_combo_dev_attr_t;

size_t hisi_sizeof_combo_dev_attr() {
    switch (chip_generation) {
    case HISI_V2A:
    case HISI_V2:
        return sizeof(V2_combo_dev_attr_t);
    case HISI_V3A:
        return sizeof(V3A_combo_dev_attr_t);
    case HISI_V3:
        return sizeof(V3_combo_dev_attr_t);
    case HISI_V4A:
        return sizeof(V4A_combo_dev_attr_t);
    case HISI_V4:
        return sizeof(V4_combo_dev_attr_t);
    default:
        fprintf(stderr, "Not implemented combo_dev_attr for %#X\n",
                chip_generation);
        exit(EXIT_FAILURE);
    }
}

static void puttabs(int cnt) {
    for (int i = 0; i < cnt; i++)
        putchar('\t');
}

#define BRACKET_OPEN                                                           \
    do {                                                                       \
        puttabs(level++);                                                      \
        puts("{");                                                             \
    } while (0)

#define BRACKET_CLOSE                                                          \
    do {                                                                       \
        puttabs(--level);                                                      \
        puts("},");                                                            \
    } while (0)

#define ENUM_PARAM(name, p)                                                    \
    puttabs(level);                                                            \
    printf("." #name " = %s,\n", name##_str((int)p))

#define ENUM_TYPED_PARAM(name, type, p)                                        \
    puttabs(level);                                                            \
    printf("." #name " = %s,\n", type##_str((int)p))

#define INT_PARAM(name, p)                                                     \
    puttabs(level);                                                            \
    printf("." #name " = %d,\n", p)

#define STRUCT_PARAM(name, fmt, ...)                                           \
    puttabs(level);                                                            \
    printf("." #name " = " fmt ",\n", __VA_ARGS__)

#define DEFINE_VAR(name)                                                       \
    puttabs(level++);                                                          \
    printf(#name " = {\n")

#define INT_ARRAY(name, data)                                                  \
    puttabs(level);                                                            \
    printf("." #name " = {");                                                  \
    for (size_t i = 0; i < ARRCNT(data); i++) {                                \
        printf("%s%d", i != 0 ? ", " : "", data[i]);                           \
    }                                                                          \
    puts("},")

static void vsync_type(lvds_vsync_attr_t *attr, int level) {
    DEFINE_VAR(.vsync_type);
    ENUM_PARAM(sync_type, attr->sync_type);
    INT_PARAM(hblank1, attr->hblank1);
    INT_PARAM(hblank2, attr->hblank2);
    BRACKET_CLOSE;
}

static void fid_type(lvds_fid_attr_t *attr, int level) {
    DEFINE_VAR(.fid_type);
    ENUM_PARAM(fid, attr->fid_type);
    ENUM_TYPED_PARAM(output_fil, bool, attr->output_fil);
    BRACKET_CLOSE;
}

static void sync_code(unsigned short *sync_code, int x, int y, int level) {
    DEFINE_VAR(.sync_code);
    for (int i = 0; i < x; i++) {
        BRACKET_OPEN;
        for (int j = 0; j < y; j++) {
            puttabs(level);
            for (int m = 0; m < SYNC_CODE_NUM; m++) {
                printf("%s%#x", m != 0 ? ", " : "{",
                       sync_code[i * x + j * y + m]);
            }
            puts("},");
        }
        BRACKET_CLOSE;
    }

    BRACKET_CLOSE;
}

static void V2_dump_lvds_dev_attr(V2_lvds_dev_attr_t *attr, int level) {
    DEFINE_VAR(.lvds_attr);
    STRUCT_PARAM(img_size, "{%d, %d}", attr->img_size.width,
                 attr->img_size.height);
    ENUM_PARAM(wdr_mode, attr->wdr_mode);
    ENUM_PARAM(sync_mode, attr->sync_mode);
    // see comment to enum declaration
    ENUM_PARAM(raw_data_type, attr->raw_data_type+1);
    ENUM_PARAM(data_endian, attr->data_endian);
    ENUM_PARAM(sync_code_endian, attr->sync_code_endian);
    INT_ARRAY(lane_id, attr->lane_id);
    sync_code((unsigned short *)&attr->sync_code, V2_LVDS_LANE_NUM, WDR_VC_NUM,
              level);
    BRACKET_CLOSE;
}

#define DUMP_LVDS_ATTR(prefix, type, lane_num)                                 \
    static void prefix##_dump_lvds_dev_attr(type *attr, int level) {           \
        DEFINE_VAR(.lvds_attr);                                                \
        ENUM_PARAM(input_data_type, attr->input_data_type);                    \
        ENUM_PARAM(wdr_mode, attr->wdr_mode);                                  \
        ENUM_PARAM(sync_mode, attr->sync_mode);                                \
        vsync_type(&attr->vsync_attr, level);                                  \
        fid_type(&attr->fid_attr, level);                                      \
        ENUM_PARAM(data_endian, attr->data_endian);                            \
        ENUM_PARAM(sync_code_endian, attr->sync_code_endian);                  \
        INT_ARRAY(lane_id, attr->lane_id);                                     \
        sync_code((unsigned short *)&attr->sync_code, lane_num, WDR_VC_NUM,    \
                  level);                                                      \
        BRACKET_CLOSE;                                                         \
    }

DUMP_LVDS_ATTR(V3A, V3A_lvds_dev_attr_t, V3A_LVDS_LANE_NUM);
DUMP_LVDS_ATTR(V4A, V4A_lvds_dev_attr_t, LVDS_LANE_NUM);

static void hisi_dump_V2combo_dev_attr(V2_combo_dev_attr_t *attr,
                                       unsigned int cmd) {
    int level = 0;
    DEFINE_VAR(combo_dev_attr_t SENSOR_ATTR);
    ENUM_PARAM(input_mode, attr->input_mode);
    if (attr->input_mode == INPUT_MODE_MIPI) {
        DEFINE_VAR(.mipi_attr);
        ENUM_PARAM(raw_data_type, attr->mipi_attr.raw_data_type);
        INT_ARRAY(lane_id, attr->mipi_attr.lane_id);
        BRACKET_CLOSE;
    } else if (attr->input_mode == INPUT_MODE_LVDS) {
        V2_dump_lvds_dev_attr(&attr->lvds_attr, level);
    }
    puts("};");
}

static void hisi_dump_V3Acombo_dev_attr(V3A_combo_dev_attr_t *attr,
                                        unsigned int cmd) {
    int level = 0;
    DEFINE_VAR(combo_dev_attr_t SENSOR_ATTR);
    INT_PARAM(devno, attr->devno);
    ENUM_PARAM(input_mode, attr->input_mode);
    ENUM_PARAM(phy_clk_share, attr->phy_clk_share);
    STRUCT_PARAM(img_rect, "{%d, %d, %d, %d}", attr->img_rect.x,
                 attr->img_rect.y, attr->img_rect.width, attr->img_rect.height);
    if (attr->input_mode == INPUT_MODE_MIPI) {
        DEFINE_VAR(.mipi_attr);
        ENUM_PARAM(raw_data_type, attr->mipi_attr.raw_data_type);
        ENUM_TYPED_PARAM(wdr_mode, mipi_wdr_mode, attr->mipi_attr.wdr_mode);
        INT_ARRAY(lane_id, attr->mipi_attr.lane_id);
        BRACKET_CLOSE;
    } else if (attr->input_mode == INPUT_MODE_LVDS) {
        V3A_dump_lvds_dev_attr(&attr->lvds_attr, level);
    }
    puts("};");
}

static void hisi_dump_V3combo_dev_attr(V3_combo_dev_attr_t *attr,
                                       unsigned int cmd) {
    int level = 0;
    DEFINE_VAR(combo_dev_attr_t SENSOR_ATTR);
    INT_PARAM(devno, attr->devno);
    ENUM_PARAM(input_mode, attr->input_mode);
    if (attr->input_mode == INPUT_MODE_MIPI) {
        DEFINE_VAR(.mipi_attr);
        ENUM_PARAM(raw_data_type, attr->mipi_attr.raw_data_type);
        ENUM_TYPED_PARAM(wdr_mode, mipi_wdr_mode, attr->mipi_attr.wdr_mode);
        INT_ARRAY(lane_id, attr->mipi_attr.lane_id);
        BRACKET_CLOSE;
    } else if (attr->input_mode == INPUT_MODE_LVDS) {
        DEFINE_VAR(.lvds_attr);
        STRUCT_PARAM(img_size, "{%d, %d}", attr->lvds_attr.img_size.width,
                     attr->lvds_attr.img_size.height);
        ENUM_PARAM(raw_data_type, attr->lvds_attr.raw_data_type);
        ENUM_PARAM(wdr_mode, attr->lvds_attr.wdr_mode);
        ENUM_PARAM(sync_mode, attr->lvds_attr.sync_mode);
        vsync_type(&attr->lvds_attr.vsync_type, level);
        fid_type(&attr->lvds_attr.fid_type, level);
        ENUM_PARAM(data_endian, attr->lvds_attr.data_endian);
        ENUM_PARAM(sync_code_endian, attr->lvds_attr.sync_code_endian);
        INT_ARRAY(lane_id, attr->lvds_attr.lane_id);
        sync_code((unsigned short *)&attr->lvds_attr.sync_code, LVDS_LANE_NUM,
                  WDR_VC_NUM, level);
        BRACKET_CLOSE;
    }
    puts("};");
}

static void hisi_dump_V4Acombo_dev_attr(V4A_combo_dev_attr_t *attr,
                                        unsigned int cmd) {
    int level = 0;
    DEFINE_VAR(combo_dev_attr_t SENSOR_ATTR);
    INT_PARAM(devno, attr->devno);
    ENUM_PARAM(input_mode, attr->input_mode);
    ENUM_PARAM(data_rate, attr->data_rate);
    STRUCT_PARAM(img_rect, "{%d, %d, %d, %d}", attr->img_rect.x,
                 attr->img_rect.y, attr->img_rect.width, attr->img_rect.height);
    if (attr->input_mode == INPUT_MODE_MIPI) {
        DEFINE_VAR(.mipi_attr);
        ENUM_PARAM(input_data_type, attr->mipi_attr.input_data_type);
        ENUM_TYPED_PARAM(wdr_mode, mipi_wdr_mode, attr->mipi_attr.wdr_mode);
        INT_ARRAY(lane_id, attr->mipi_attr.lane_id);
        BRACKET_CLOSE;
    } else if (attr->input_mode == INPUT_MODE_LVDS) {
        V4A_dump_lvds_dev_attr(&attr->lvds_attr, level);
    }
    puts("};");
}

static void hisi_dump_V4combo_dev_attr(V4_combo_dev_attr_t *attr,
                                       unsigned int cmd) {
    int level = 0;
    DEFINE_VAR(combo_dev_attr_t SENSOR_ATTR);
    INT_PARAM(devno, attr->devno);
    ENUM_PARAM(input_mode, attr->input_mode);
    ENUM_PARAM(data_rate, attr->data_rate);
    STRUCT_PARAM(img_rect, "{%d, %d, %d, %d}", attr->img_rect.x,
                 attr->img_rect.y, attr->img_rect.width, attr->img_rect.height);
    if (attr->input_mode == INPUT_MODE_MIPI) {
        DEFINE_VAR(.mipi_attr);
        ENUM_PARAM(input_data_type, attr->mipi_attr.input_data_type);
        ENUM_TYPED_PARAM(wdr_mode, mipi_wdr_mode, attr->mipi_attr.wdr_mode);
        INT_ARRAY(lane_id, attr->mipi_attr.lane_id);
        BRACKET_CLOSE;
    } else if (attr->input_mode == INPUT_MODE_LVDS) {
        DEFINE_VAR(.lvds_attr);
        ENUM_PARAM(input_data_type, attr->lvds_attr.input_data_type);
        ENUM_PARAM(wdr_mode, attr->lvds_attr.wdr_mode);
        ENUM_PARAM(sync_mode, attr->lvds_attr.sync_mode);
        vsync_type(&attr->lvds_attr.vsync_attr, level);
        fid_type(&attr->lvds_attr.fid_attr, level);
        ENUM_PARAM(data_endian, attr->lvds_attr.data_endian);
        ENUM_PARAM(sync_code_endian, attr->lvds_attr.sync_code_endian);
        INT_ARRAY(lane_id, attr->lvds_attr.lane_id);
        sync_code((unsigned short *)&attr->lvds_attr.sync_code, LVDS_LANE_NUM,
                  V4_WDR_VC_NUM, level);
        BRACKET_CLOSE;
    }
    puts("};");
}

void hisi_dump_combo_dev_attr(void *ptr, unsigned int cmd) {
    switch ((cmd >> 16) & 0x1ff) {
    case sizeof(V2_combo_dev_attr_t):
        return hisi_dump_V2combo_dev_attr(ptr, cmd);
    case sizeof(V3A_combo_dev_attr_t):
        return hisi_dump_V3Acombo_dev_attr(ptr, cmd);
    case sizeof(V3_combo_dev_attr_t):
        return hisi_dump_V3combo_dev_attr(ptr, cmd);
    case sizeof(V4A_combo_dev_attr_t):
        return hisi_dump_V4Acombo_dev_attr(ptr, cmd);
    case sizeof(V4_combo_dev_attr_t):
        return hisi_dump_V4combo_dev_attr(ptr, cmd);
    default:
        fprintf(stderr, "Not implemented\n");
    }
}
