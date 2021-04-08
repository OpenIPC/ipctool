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
#include "ram.h"
#include "tools.h"

static unsigned char sony_addrs[] = {0x34, 0};
static unsigned char soi_addrs[] = {0x80, 0};
static unsigned char onsemi_addrs[] = {0x20, 0};
static unsigned char ssens_addrs[] = {0x60, 0};
static unsigned char omni_addrs[] = {0x60, 0};

sensor_addr_t hisi_possible_i2c_addrs[] = {
    {SENSOR_SONY, sony_addrs},       {SENSOR_SOI, soi_addrs},
    {SENSOR_ONSEMI, onsemi_addrs},   {SENSOR_SMARTSENS, ssens_addrs},
    {SENSOR_OMNIVISION, omni_addrs}, {0, NULL}};

static float hisi_get_temp();

int hisi_open_sensor_fd() {
    int adapter_nr = 0; /* probably dynamically determined */
    char filename[FILENAME_MAX];

    snprintf(filename, sizeof(filename), "/dev/i2c-%d", adapter_nr);

    return common_open_sensor_fd(filename);
}

void hisi_close_sensor_fd(int fd) { close(fd); }

int hisi_gen1_open_sensor_fd() { return common_open_sensor_fd("/dev/hi_i2c"); }

// Set I2C slave address
int hisi_sensor_i2c_change_addr(int fd, unsigned char addr) {
    // use i2c address shift only for generations other than 2
    if (chip_generation != HISI_V2) {
        addr >>= 1;
    }

    int ret = ioctl(fd, I2C_SLAVE_FORCE, addr);
    if (ret < 0) {
        fprintf(stderr, "CMD_SET_DEV error!\n");
        return ret;
    }
    return ret;
}

#define I2C_16BIT_REG 0x0709  /* 16BIT REG WIDTH */
#define I2C_16BIT_DATA 0x070a /* 16BIT DATA WIDTH */
int hisi_gen2_set_width(int fd, unsigned int reg_width,
                        unsigned int data_width) {
    int ret;
    if (reg_width == 2)
        ret = ioctl(fd, I2C_16BIT_REG, 1);
    else
        ret = ioctl(fd, I2C_16BIT_REG, 0);
    if (ret < 0) {
        fprintf(stderr, "CMD_SET_REG_WIDTH error!\n");
        return -1;
    }

    if (data_width == 2)
        ret = ioctl(fd, I2C_16BIT_DATA, 1);
    else
        ret = ioctl(fd, I2C_16BIT_DATA, 0);

    if (ret < 0) {
        fprintf(stderr, "CMD_SET_DATA_WIDTH error!\n");
        return -1;
    }
    return 0;
}

int hisi_gen2_sensor_write_register(int fd, unsigned char i2c_addr,
                                    unsigned int reg_addr,
                                    unsigned int reg_width, unsigned int data,
                                    unsigned int data_width) {

    int ret;
    unsigned int index = 0;
    unsigned int reg_value;
    char buf[4];

    if (hisi_gen2_set_width(fd, reg_width, data_width))
        return -1;

    if (reg_width == 2) {
        buf[index] = reg_addr & 0xff;
        index++;
        buf[index] = (reg_addr >> 8) & 0xff;
        index++;
    } else {
        buf[index] = reg_addr & 0xff;
        index++;
    }

    if (data_width == 2) {
        buf[index] = reg_value & 0xff;
        index++;
        buf[index] = (reg_value >> 8) & 0xff;
        index++;
    } else {
        buf[index] = reg_value & 0xff;
        index++;
    }

    ret = write(fd, buf, reg_width + data_width);
    if (ret < 0) {
        fprintf(stderr, "I2C_WRITE error!\n");
        return -1;
    }

    return 0;
}

int hisi_sensor_write_register(int fd, unsigned char i2c_addr,
                               unsigned int reg_addr, unsigned int reg_width,
                               unsigned int data, unsigned int data_width) {
    int idx = 0;
    int ret;
    char buf[8];

    if (reg_width == 2) {
        buf[idx] = (reg_addr >> 8) & 0xff;
        idx++;
        buf[idx] = reg_addr & 0xff;
        idx++;
    } else {
        buf[idx] = reg_addr & 0xff;
        idx++;
    }

    if (data_width == 2) {
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

int hisi_gen2_sensor_read_register(int fd, unsigned char i2c_addr,
                                   unsigned int reg_addr,
                                   unsigned int reg_width,
                                   unsigned int data_width) {
    int ret;
    char recvbuf[4];
    unsigned int data;

    if (hisi_gen2_set_width(fd, reg_width, data_width))
        return -1;

    if (reg_width == 2) {
        recvbuf[0] = reg_addr & 0xff;
        recvbuf[1] = (reg_addr >> 8) & 0xff;
    } else {
        recvbuf[0] = reg_addr & 0xff;
    }

    ret = read(fd, recvbuf, reg_width);
    if (ret < 0) {
        return -1;
    }

    if (data_width == 2) {
        data = recvbuf[0] | (recvbuf[1] << 8);
    } else
        data = recvbuf[0];

    return data;
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

#define SSP_READ_ALT 0x1
int sony_ssp_read_register(int fd, unsigned char i2c_addr,
                           unsigned int reg_addr, unsigned int reg_width,
                           unsigned int data_width) {
    unsigned int data = (unsigned int)(((reg_addr & 0xffff) << 8));
    int ret = ioctl(fd, SSP_READ_ALT, &data);
    return data & 0xff;
}

struct spi_ioc_transfer {
    __u64 tx_buf;
    __u64 rx_buf;

    __u32 len;
    __u32 speed_hz;

    __u16 delay_usecs;
    __u8 bits_per_word;
    __u8 cs_change;
    __u32 pad;
};

#define SPI_IOC_MAGIC 'k'
#define SPI_MSGSIZE(N)                                                         \
    ((((N) * (sizeof(struct spi_ioc_transfer))) < (1 << _IOC_SIZEBITS))        \
         ? ((N) * (sizeof(struct spi_ioc_transfer)))                           \
         : 0)
#define SPI_IOC_MESSAGE(N) _IOW(SPI_IOC_MAGIC, 0, char[SPI_MSGSIZE(N)])

int hisi_gen3_spi_read_register(int fd, unsigned char i2c_addr,
                                unsigned int reg_addr, unsigned int reg_width,
                                unsigned int data_width) {
    int ret = 0;
    struct spi_ioc_transfer mesg[1];
    unsigned char tx_buf[8] = {0};
    unsigned char rx_buf[8] = {0};

    tx_buf[0] = (reg_addr & 0xff00) >> 8;
    tx_buf[0] |= 0x80;
    tx_buf[1] = reg_addr & 0xff;
    tx_buf[2] = 0;
    memset(mesg, 0, sizeof(mesg));
    mesg[0].tx_buf = (__u64)((__u32)&tx_buf);
    mesg[0].len = 3;
    mesg[0].rx_buf = (__u64)((__u32)&rx_buf);
    mesg[0].cs_change = 1;

    ret = ioctl(fd, SPI_IOC_MESSAGE(1), mesg);
    if (ret < 0) {
        printf("SPI_IOC_MESSAGE error \n");
        return -1;
    }

    return rx_buf[2];
}

static unsigned long hisi_media_mem() {
    char buf[256];

    if (!get_regex_line_from_file("/proc/media-mem", "total size=([0-9]+)KB",
                                  buf, sizeof(buf))) {
        return 0;
    }
    return strtoul(buf, NULL, 10);
}

uint32_t hisi_totalmem(unsigned long *media_mem) {
    *media_mem = hisi_media_mem();
    return *media_mem + kernel_mem();
}

static char printk_state[16];
#define PRINTK_FILE "/proc/sys/kernel/printk"
static void disable_printk() {
    if (*printk_state)
        return;

    FILE *fp = fopen(PRINTK_FILE, "rw+");
    if (!fp)
        return;
    const char *ret;
    ret = fgets(printk_state, sizeof(printk_state) - 1, fp);
    // We cannot use rewind() here
    fclose(fp);
    if (!ret)
        return;

    fp = fopen(PRINTK_FILE, "w");
    fprintf(fp, "0 0 0 0\n");
    fclose(fp);
}

struct EV300_PERI_CRG60 {
    bool sensor0_cken : 1;
    unsigned int sensor0_srst_req : 1;
    unsigned int sensor0_cksel : 3;
    bool sensor0_ctrl_cken : 1;
    unsigned int sensor0_ctrl_srst_req : 1;
};

#define CV300_MUX_BASE 0x12040000
#define CV300_CRG_BASE 0x12010000

const uint32_t CV300_MUX30_ADDR = CV300_MUX_BASE + 0x0030;
const uint32_t CV300_MUX2C_ADDR = CV300_MUX_BASE + 0x002c;
const uint32_t CV300_MUX14_ADDR = CV300_MUX_BASE + 0x0038;
const uint32_t CV300_MUX15_ADDR = CV300_MUX_BASE + 0x003c;

const uint32_t CV300_PERI_CRG11_ADDR = CV300_CRG_BASE + 0x002c;

static void v3_ensure_sensor_enabled() {
    uint32_t reg;
    if (mem_reg(CV300_MUX30_ADDR, (uint32_t *)&reg, OP_READ)) {
        if (!reg) {
            reg = 3;
            mem_reg(CV300_MUX30_ADDR, (uint32_t *)&reg, OP_WRITE);
            mem_reg(CV300_MUX2C_ADDR, (uint32_t *)&reg, OP_WRITE);
            reg = 1;
            mem_reg(CV300_MUX14_ADDR, (uint32_t *)&reg, OP_WRITE);
            mem_reg(CV300_MUX15_ADDR, (uint32_t *)&reg, OP_WRITE);

            reg = 0xC06800D;
            mem_reg(CV300_PERI_CRG11_ADDR, (uint32_t *)&reg, OP_WRITE);
            usleep(100 * 1000);
            reg = 0x406800D;
            mem_reg(CV300_PERI_CRG11_ADDR, (uint32_t *)&reg, OP_WRITE);
        }
    }
}

const unsigned int EV300_PERI_CRG60_ADDR = 0x120100F0;
static struct EV300_PERI_CRG60 peri_crg60;
static bool crg60_changed;
static void v4_ensure_sensor_enabled() {
    struct EV300_PERI_CRG60 crg60;
    if (mem_reg(EV300_PERI_CRG60_ADDR, (uint32_t *)&crg60, OP_READ)) {
        if (!crg60.sensor0_cken) {
            peri_crg60 = crg60;
            // 1: clock enabled
            crg60.sensor0_cken = true;
            // 0: reset deasserted
            crg60.sensor0_srst_req = false;
            mem_reg(EV300_PERI_CRG60_ADDR, (uint32_t *)&crg60, OP_WRITE);
            crg60_changed = true;
        }
    }
}

static void v4_ensure_sensor_restored() {
    if (crg60_changed) {
        mem_reg(EV300_PERI_CRG60_ADDR, (uint32_t *)&peri_crg60, OP_WRITE);
    }
}

static void restore_printk() {
    if (!*printk_state)
        return;

    FILE *fp = fopen(PRINTK_FILE, "w");
    fprintf(fp, "%s", printk_state);
    fclose(fp);
}

static void hisi_hal_cleanup() {
    if (chip_generation == HISI_V4)
        v4_ensure_sensor_restored();
    restore_printk();
}

void setup_hal_hisi() {
    disable_printk();
    if (chip_generation == HISI_V3)
        v3_ensure_sensor_enabled();
    else if (chip_generation == HISI_V4)
        v4_ensure_sensor_enabled();

    open_sensor_fd = hisi_open_sensor_fd;
    close_sensor_fd = hisi_close_sensor_fd;
    hal_cleanup = hisi_hal_cleanup;
    sensor_i2c_change_addr = hisi_sensor_i2c_change_addr;
    if (chip_generation == HISI_V1) {
        open_sensor_fd = hisi_gen1_open_sensor_fd;
        sensor_read_register = xm_sensor_read_register;
        sensor_write_register = xm_sensor_write_register;
    } else if (chip_generation == HISI_V2) {
        sensor_read_register = hisi_gen2_sensor_read_register;
        sensor_write_register = hisi_gen2_sensor_write_register;
    } else {
        sensor_read_register = hisi_sensor_read_register;
        sensor_write_register = hisi_sensor_write_register;
    }
    possible_i2c_addrs = hisi_possible_i2c_addrs;
    strcpy(short_manufacturer, "HI");
    hal_temperature = hisi_get_temp;
}

int hisi_SYS_DRV_GetChipId() {
    int fd = open("/dev/sys", O_RDWR);
    if (fd < 0)
        return -1;

    uint32_t id;
    int res = ioctl(fd, 0x80045910, &id);
    if (res)
        return -1;
    return id;
}

static uint32_t hisi_reg_temp(uint32_t read_addr, int temp_bitness,
                              uint32_t prep_addr, uint32_t prep_val) {
    uint32_t val;

    if (mem_reg(prep_addr, &val, OP_READ)) {
        if (!val) {
            val = prep_val;
            mem_reg(prep_addr, &val, OP_WRITE);
            usleep(100000);
        }
    }

    if (mem_reg(read_addr, &val, OP_READ)) {
        return val & ((1 << temp_bitness) - 1);
    }
    return 0;
}

static float hisi_get_temp() {
    float tempo;
    switch (chip_generation) {
    case HISI_V2:
        tempo = hisi_reg_temp(0x20270114, 8, 0x20270110, 0x60FA0000);
        tempo = ((tempo * 180) / 256) - 40;
        break;
    case HISI_V3:
        tempo = hisi_reg_temp(0x120300A4, 16, 0x1203009C, 0x60FA0000);
        tempo = ((tempo - 125.0) / 806) * 165 - 40;
        break;
    case HISI_V4:
        tempo = hisi_reg_temp(0x120280BC, 16, 0x120280B4, 0xC3200000);
        tempo = ((tempo - 117) / 798) * 165 - 40;
        break;
    default:
        return NAN;
    }

    return tempo;
}

#ifndef STANDALONE_LIBRARY

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

const unsigned int CV100_PERI_CRG12_ADDR = 0x20030030;
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

const unsigned int CV200_MISC_CTRL1_ADDR = 0x20120004;
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
const uint32_t CV300_LVDS0_IMGSIZE_ADDR = CV300_MIPI_BASE + 0x130C;
struct LVDS0_IMGSIZE {
    unsigned int lvds_imgwidth_lane : 16;
    unsigned int lvds_imgheight : 16;
};

const uint32_t CV300_LVDS0_WDR_ADDR = CV300_MIPI_BASE + 0x1300;
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
} lvds_sync_mode_e;

typedef enum {
    LVDS_ENDIAN_LITTLE = 0x0,
    LVDS_ENDIAN_BIG = 0x1,
} lvds_bit_endian;

const uint32_t CV300_LVDS0_CTRL_ADDR = CV300_MIPI_BASE + 0x1304;
struct LVDS0_CTRL {
    lvds_sync_mode_e lvds_sync_mode : 1;
    unsigned int res0 : 3;
    raw_data_type_e lvds_raw_type : 3;
    unsigned int res1 : 1;
    lvds_bit_endian lvds_pix_big_endian : 1;
    lvds_bit_endian lvds_code_big_endian : 1;
    unsigned int res2 : 2;
    bool lvds_crop_en : 1;
    unsigned int res3 : 3;
    unsigned int lvds_split_mode : 3;
};

const uint32_t CV200_MIPI_LANES_NUM_ADDR = 0x20680000 + 0x1030;
struct CV200_MIPI_LANES_NUM {
    unsigned int lane_num : 2;
};

static size_t cv200_mipi_lanes_num() {
    struct CV200_MIPI_LANES_NUM lnum;
    mem_reg(CV200_MIPI_LANES_NUM_ADDR, (uint32_t *)&lnum, OP_READ);
    return lnum.lane_num + 1;
}

const uint32_t CV300_MIPI0_LANES_NUM_ADDR = CV300_MIPI_BASE + 0x1004;
struct CV300_MIPI0_LANES_NUM {
    unsigned int lane_num : 3;
};

static size_t cv300_mipi_lanes_num() {
    struct CV300_MIPI0_LANES_NUM lnum;
    mem_reg(CV300_MIPI0_LANES_NUM_ADDR, (uint32_t *)&lnum, OP_READ);
    return lnum.lane_num + 1;
}

const uint32_t EV200_MIPI_LANES_NUM_ADDR = 0x11240000 + 0x1004;
struct EV200_MIPI_LANES_NUM {
    unsigned int lane_num : 2;
};

static size_t ev200_mipi_lanes_num() {
    struct EV200_MIPI_LANES_NUM lnum;
    mem_reg(EV200_MIPI_LANES_NUM_ADDR, (uint32_t *)&lnum, OP_READ);
    return lnum.lane_num + 1;
}

const uint32_t EV300_MIPI_LANES_NUM_ADDR = 0x11240000 + 0x1004;
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
    case HISI_V2:
        return cv200_mipi_lanes_num();
    case HISI_V3:
        return cv300_mipi_lanes_num();
    case HISI_V4:
        if (!strcmp(chip_id, "3516EV200"))
            return ev200_mipi_lanes_num();
        else
            return ev300_mipi_lanes_num();
    }
    return 0;
}

#define CV200_MIPI_BASE 0x20680000
const uint32_t CV200_LANE_ID_LINK0_ADDR = CV200_MIPI_BASE + 0x1014;
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

const uint32_t CV300_ALIGN0_LANE_ID_ADDR = CV300_MIPI_BASE + 0x1600;
struct CV300_ALIGN0_LANE_ID {
    unsigned int lane0_id : 4;
    unsigned int lane1_id : 4;
    unsigned int lane2_id : 4;
    unsigned int lane3_id : 4;
};

#define EV300_MIPI_BASE 0x11240000
const uint32_t EV200_LANE_ID0_CHN_ADDR = EV300_MIPI_BASE + 0x1800;
struct EV200_LANE_ID0_CHN {
    unsigned int lane0_id : 4;
    unsigned int res0 : 4;
    unsigned int lane2_id : 4;
    unsigned int res1 : 4;
};

const uint32_t EV300_LANE_ID0_CHN_ADDR = EV300_MIPI_BASE + 0x1800;
struct EV300_LANE_ID0_CHN {
    unsigned int lane0_id : 4;
    unsigned int lane1_id : 4;
    unsigned int lane2_id : 4;
    unsigned int lane3_id : 4;
};

static void ev300_enum_lanes(cJSON *j_inner, size_t lanes) {
    if (!strcmp(chip_id, "3516EV200")) {
        struct EV200_LANE_ID0_CHN lid;
        if (mem_reg(EV200_LANE_ID0_CHN_ADDR, (uint32_t *)&lid, OP_READ)) {
            cJSON *j_lanes = cJSON_AddArrayToObject(j_inner, "laneId");
            cJSON_AddItemToArray(j_lanes, cJSON_CreateNumber(lid.lane0_id));
            if (lanes > 1)
                cJSON_AddItemToArray(j_lanes, cJSON_CreateNumber(lid.lane2_id));
        }

    } else {
        struct EV300_LANE_ID0_CHN lid;
        if (mem_reg(EV300_LANE_ID0_CHN_ADDR, (uint32_t *)&lid, OP_READ)) {
            cJSON *j_lanes = cJSON_AddArrayToObject(j_inner, "laneId");
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
                          lvds_bit_endian val) {
    if (val == LVDS_ENDIAN_LITTLE)
        ADD_PARAM(param, "LVDS_ENDIAN_LITTLE")
    else
        ADD_PARAM(param, "LVDS_ENDIAN_BIG");
}

#define LO(x) x & 0xffff
#define HI(x) x & 0xffff0000 >> 16

static void cv300_enum_sync_codes(cJSON *j_inner) {
    uint32_t addr = 0x11301320;
    uint32_t end_addr = 0x1130141C;

    cJSON *j_codes = cJSON_AddArrayToObject(j_inner, "syncCode");

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

const unsigned int CV300_MISC_CTRL0_ADDR = 0x12030000;
static void hisi_cv300_sensor_data(cJSON *j_root) {
    cJSON *j_inner = cJSON_CreateObject();

    struct CV300_MISC_CTRL0 ctrl0;
    bool is_lvds = false;
    if (mem_reg(CV300_MISC_CTRL0_ADDR, (uint32_t *)&ctrl0, OP_READ)) {
        switch (ctrl0.mipi_phy_mode) {
        case CV300_PHY_CMOS_MODE:
            ADD_PARAM("type", "DC");
            break;
        case CV300_PHY_LVDS_MODE:
            ADD_PARAM("type", "LVDS");
            is_lvds = true;
        case CV300_PHY_MIPI_MODE:
            if (!is_lvds)
                ADD_PARAM("type", "MIPI");
            /* .mipi_attr =
            {
                .raw_data_type = RAW_DATA_12BIT,
                .wdr_mode      = HI_WDR_MODE_NONE,
                .lane_id       ={0, 1, 2, 3}
            }
            */

            size_t lanes = mipi_lanes_num();

            struct CV300_ALIGN0_LANE_ID lid;
            if (mem_reg(CV300_ALIGN0_LANE_ID_ADDR, (uint32_t *)&lid, OP_READ)) {
                cJSON *j_lanes = cJSON_AddArrayToObject(j_inner, "laneId");
                cJSON_AddItemToArray(j_lanes, cJSON_CreateNumber(lid.lane0_id));
                if (lanes > 1)
                    cJSON_AddItemToArray(j_lanes,
                                         cJSON_CreateNumber(lid.lane1_id));
                if (lanes > 2)
                    cJSON_AddItemToArray(j_lanes,
                                         cJSON_CreateNumber(lid.lane2_id));
                if (lanes > 3)
                    cJSON_AddItemToArray(j_lanes,
                                         cJSON_CreateNumber(lid.lane3_id));
            }

            struct CV300_LVDS0_WDR wdr;
            if (mem_reg(CV300_LVDS0_WDR_ADDR, (uint32_t *)&wdr, OP_READ)) {
                ADD_PARAM_NUM("lvdsWdrEn", wdr.lvds_wdr_en);
                ADD_PARAM_NUM("lvdsWdrMode", wdr.lvds_wdr_mode);
                ADD_PARAM_NUM("lvdsWdrNum", wdr.lvds_wdr_num);
            }

            struct LVDS0_CTRL lvds0_ctrl;
            if (mem_reg(CV300_LVDS0_CTRL_ADDR, (uint32_t *)&lvds0_ctrl,
                        OP_READ)) {

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
                    ADD_PARAM("rawDataType", raw);

                if (is_lvds) {
                    if (lvds0_ctrl.lvds_sync_mode == LVDS_SYNC_MODE_SOF)
                        ADD_PARAM("syncMode", "LVDS_SYNC_MODE_SOF")
                    else
                        ADD_PARAM("syncMode", "LVDS_SYNC_MODE_SAV");

                    lvds_code_set(j_inner, "dataEndian",
                                  lvds0_ctrl.lvds_pix_big_endian);
                    lvds_code_set(j_inner, "syncCodeEndian",
                                  lvds0_ctrl.lvds_code_big_endian);
                    cv300_enum_sync_codes(j_inner);
                }
            }

            break;
        default:
            return;
        }
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

const unsigned int CV200_PERI_CRG11_ADDR = 0x2003002c;
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

const unsigned int EV300_MISC_CTRL6_ADDR = 0x12028018;
struct EV300_MISC_CTRL6 {
    enum EV300_MIPI_PHY mipirx0_work_mode : 2;
};

const uint32_t EV300_MIPI_IMGSIZE = EV300_MIPI_BASE + 0x1224;
struct CV300_EV300_MIPI_IMGSIZE {
    unsigned int mipi_imgwidth : 16;
    unsigned int mipi_imgheight : 16;
};

const uint32_t EV300_MIPI_DI_1_ADDR = EV300_MIPI_BASE + 0x1010;
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
            ADD_PARAM("input_data_type", ev300_mipi_raw_data(di1.di0_dt));

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
    for (uint32_t id_addr = base_id_addr + 5 * 4; id_addr >= base_id_addr;
         id_addr -= 4) {
        uint32_t val;
        if (!mem_reg(id_addr, &val, OP_READ))
            return false;
        int outsz = snprintf(buf, len, "%08x", val);
        buf += outsz;
        len -= outsz;
    }
    return true;
}

// muxctrl_reg23 is a multiplexing control register for the MII_TXCK pin.
struct CV100_MUXCTRL_REG23 {
    unsigned int muxctrl_reg23 : 2;
};

enum CV100_MUX_MII_TXCK {
    CV100_GPIO3_3 = 0,
    CV100_MII_TXCK,
    CV100_VOU1120_DATA7,
    CV100_RMII_CLK,
};

const unsigned int CV100_MUXCTRL_ADDR = 0x200F005C;
const char *hisi_cv100_get_mii_mux() {
    struct CV100_MUXCTRL_REG23 muxctrl_reg23;
    bool res = mem_reg(CV100_MUXCTRL_ADDR, (uint32_t *)&muxctrl_reg23, OP_READ);
    if (res) {
        switch (muxctrl_reg23.muxctrl_reg23) {
        case CV100_MII_TXCK:
            return "mii";
        case CV100_RMII_CLK:
            return "rmii";
        default:
            return NULL;
        }
    }

    return NULL;
}

const uint32_t CV300_ISP_AF_CFG_ADDR = 0x12200;
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
// ev300 -  0x1014 + PT_N x 0x100

const uint32_t PT_INTF_MOD_OFFSET = 0x100;
const bool hisi_vi_is_not_running(cJSON *j_inner) {
    uint32_t addr = 0, PT_N = 0;
    uint32_t base = 0;
    switch (chip_generation) {
    case HISI_V1:
    case HISI_V2:
        base = 0x20580000;
        addr = 0x20580000 + 0x0100;
        break;
    case HISI_V3:
        base = 0x11380000;
        addr = 0x11380000 + 0x0100;
        break;
    case HISI_V4:
        base = 0x11000000 + 0x1000;
        addr = 0x11000000 + 0x1000 + PT_N * 0x100;
        break;
    default:
        return false;
    }
    struct PT_INTF_MOD reg;
    if (mem_reg(addr, (uint32_t *)&reg, OP_READ)) {
        if (!reg.enable)
            ADD_PARAM("viState", "down");

        return !reg.enable;
    }

    return false;
}

static void determine_sensor_data_type(cJSON *j_inner) {
    switch (chip_generation) {
    case HISI_V1:
        return hisi_cv100_sensor_data(j_inner);
    case HISI_V2:
        return hisi_cv200_sensor_data(j_inner);
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
    case HISI_V2:
        return hisi_cv200_sensor_clock(j_inner);
    case HISI_V3:
        return hisi_cv300_sensor_clock(j_inner);
    case HISI_V4:
        return hisi_ev300_sensor_clock(j_inner);
    }
}

void hisi_vi_information(sensor_ctx_t *ctx) {
    if (hisi_vi_is_not_running(ctx->j_sensor))
        return;

    determine_sensor_data_type(ctx->j_sensor);
    determine_sensor_clock(ctx->j_sensor);
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
#endif
