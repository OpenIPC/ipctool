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
#include "hal_common.h"
#include "hisi/ethernet.h"
#include "hisi/ptrace.h"
#include "ram.h"
#include "tools.h"

typedef unsigned int combo_dev_t;

typedef enum {
    RAW_UNKNOWN = 0,
    RAW_DATA_8BIT,
    RAW_DATA_10BIT,
    RAW_DATA_12BIT,
    RAW_DATA_14BIT,
    RAW_DATA_16BIT,
} raw_data_type_e;

const char *raw_data_type_e_str(raw_data_type_e val) {
    switch (val) {
    case RAW_UNKNOWN:
        return "RAW_UNKNOWN";
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
    LVDS_SYNC_MODE_SOF = 0, /* sensor SOL, EOL, SOF, EOF */
    LVDS_SYNC_MODE_SAV,     /* SAV, EAV */
} lvds_sync_mode_t;

typedef enum {
    LVDS_ENDIAN_LITTLE = 0x0,
    LVDS_ENDIAN_BIG = 0x1,
} lvds_bit_endian_t;

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

const char *input_mode_t_str(input_mode_t val) {
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

const char *mipi_data_rate_t_str(mipi_data_rate_t val) {
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

const char *data_type_t_str(data_type_t val) {
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

const char *mipi_wdr_mode_t_str(mipi_wdr_mode_t val) {
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

const char *phy_clk_share_e_str(phy_clk_share_e val) {
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

typedef enum {
    LVDS_VSYNC_NORMAL = 0x00,
    LVDS_VSYNC_SHARE = 0x01,
    LVDS_VSYNC_HCONNECT = 0x02,
} lvds_vsync_type_t;

typedef struct {
    lvds_vsync_type_t sync_type;
    unsigned short hblank1;
    unsigned short hblank2;
} lvds_vsync_attr_t;

typedef struct {
    raw_data_type_e raw_data_type;
    short lane_id[V2A23A_MIPI_LANE_NUM];
} V2A_mipi_dev_attr_t;

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
    LVDS_FID_IN_DATA = 0x02, /* frame identification id in first data */
} lvds_fid_type_t;

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
} lvds_dev_attr_t;

typedef struct {
    input_mode_t input_mode;
    union {
        V2A_mipi_dev_attr_t mipi_attr;
        lvds_dev_attr_t lvds_attr;
    };
} V2A_combo_dev_attr_t;

typedef struct {
    input_mode_t input_mode;
    union {
        V2_mipi_dev_attr_t mipi_attr;
        lvds_dev_attr_t lvds_attr;
    };
} V2_combo_dev_attr_t;

typedef struct {
    combo_dev_t devno;
    input_mode_t input_mode;
    phy_clk_share_e phy_clk_share;
    img_rect_t img_rect;
    union {
        V3A_mipi_dev_attr_t mipi_attr;
        lvds_dev_attr_t lvds_attr;
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
        lvds_dev_attr_t lvds_attr;
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

static void print_shortarray(short *data, size_t size) {
    printf("\t\t\t{");
    for (int i = 0; i < size; i++) {
        printf("%s%d", i != 0 ? ", " : "", data[i]);
    }
    printf("}\n");
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
        puts("}");                                                             \
    } while (0)

static void hisi_dump_V3combo_dev_attr(V3_combo_dev_attr_t *attr,
                                       unsigned int cmd) {
    int level = 0;
    puts("combo_dev_attr_t SENSOR_ATTR =");
    BRACKET_OPEN;
    printf("\t.devno = %d,\n"
           "\t.input_mode = %s,\n",
           attr->devno, input_mode_t_str(attr->input_mode));
    BRACKET_OPEN;
    if (attr->input_mode == INPUT_MODE_MIPI) {
        printf("\t\t.mipi_attr =\n");
        BRACKET_OPEN;
        printf("\t\t\t%s,\n"
               "\t\t\t%s,\n",
               raw_data_type_e_str(attr->mipi_attr.raw_data_type),
               mipi_wdr_mode_t_str(attr->mipi_attr.wdr_mode));
        print_shortarray(attr->mipi_attr.lane_id,
                         ARRAY_SIZE(attr->mipi_attr.lane_id));
        BRACKET_CLOSE;
    } else if (attr->input_mode == INPUT_MODE_LVDS) {
    }
    BRACKET_CLOSE;
    puts("};");
}

static void hisi_dump_V4combo_dev_attr(V4_combo_dev_attr_t *attr,
                                       unsigned int cmd) {
    int level = 0;
    puts("combo_dev_attr_t SENSOR_ATTR =");
    BRACKET_OPEN;
    printf("\t.devno = %d,\n"
           "\t.input_mode = %s,\n"
           "\t.data_rate = %s,\n"
           "\t.img_rect = {%d, %d, %d, %d},\n",
           attr->devno, input_mode_t_str(attr->input_mode),
           mipi_data_rate_t_str(attr->data_rate), attr->img_rect.x,
           attr->img_rect.y, attr->img_rect.width, attr->img_rect.height);
    BRACKET_OPEN;
    if (attr->input_mode == INPUT_MODE_MIPI) {
        printf("\t\t.mipi_attr =\n");
        BRACKET_OPEN;
        printf("\t\t\t%s,\n"
               "\t\t\t%s,\n",
               data_type_t_str(attr->mipi_attr.input_data_type),
               mipi_wdr_mode_t_str(attr->mipi_attr.wdr_mode));
        print_shortarray(attr->mipi_attr.lane_id,
                         ARRAY_SIZE(attr->mipi_attr.lane_id));
        BRACKET_CLOSE;
    } else if (attr->input_mode == INPUT_MODE_LVDS) {
    }
    BRACKET_CLOSE;
    puts("};");
}

void hisi_dump_combo_dev_attr(void *ptr, unsigned int cmd) {
    switch ((cmd >> 16) & 0xff) {
    case sizeof(V3_combo_dev_attr_t):
        return hisi_dump_V3combo_dev_attr(ptr, cmd);
    case sizeof(V4_combo_dev_attr_t):
        return hisi_dump_V4combo_dev_attr(ptr, cmd);
    default:
        fprintf(stderr, "Not implemented\n");
    }
}
