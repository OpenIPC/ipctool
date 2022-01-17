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
#include "ram.h"
#include "tools.h"

static unsigned char sony_addrs[] = {0x34, 0};
static unsigned char soi_addrs[] = {0x80, 0x60, 0};
static unsigned char onsemi_addrs[] = {0x20, 0};
static unsigned char ssens_addrs[] = {0x60, 0};
static unsigned char omni_addrs[] = {0x60, 0x6c, 0x42, 0};
static unsigned char gc_addrs[] = {0x6e, 0};
static unsigned char superpix_addrs[] = {0x79, 0};

sensor_addr_t hisi_possible_i2c_addrs[] = {
    {SENSOR_SONY, sony_addrs},         {SENSOR_SOI, soi_addrs},
    {SENSOR_ONSEMI, onsemi_addrs},     {SENSOR_SMARTSENS, ssens_addrs},
    {SENSOR_OMNIVISION, omni_addrs},   {SENSOR_GALAXYCORE, gc_addrs},
    {SENSOR_SUPERPIX, superpix_addrs}, {0, NULL}};

static float hisi_get_temp();

int hisi_open_sensor_fd() {
    int adapter_nr = 0; /* probably dynamically determined */
    char filename[FILENAME_MAX];

    snprintf(filename, sizeof(filename), "/dev/i2c-%d", adapter_nr);

    return universal_open_sensor_fd(filename);
}

int hisi_gen1_open_sensor_fd() {
    return universal_open_sensor_fd("/dev/hi_i2c");
}

// Set I2C slave address
int hisi_gen2_sensor_i2c_change_addr(int fd, unsigned char addr) {
    if (ioctl(fd, I2C_SLAVE_FORCE, addr) < 0) {
        return -1;
    }
    return 0;
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
        buf[index] = data & 0xff;
        index++;
        buf[index] = (data >> 8) & 0xff;
        index++;
    } else {
        buf[index] = data & 0xff;
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

    ret = write(fd, buf, reg_width + data_width);
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
    struct i2c_rdwr_ioctl_data rdwr;
    struct i2c_msg msg[2];
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
    mesg[0].tx_buf = (__u64)(long)&tx_buf;
    mesg[0].len = 3;
    mesg[0].rx_buf = (__u64)(long)&rx_buf;
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

static bool is_cma_allocator() {
    char buf[64];
    if (get_regex_line_from_file("/proc/cmdline", "mmz_allocator=(\\w+)", buf,
                                 sizeof(buf))) {
        if (!strcmp(buf, "cma"))
            return true;
    }

    return false;
}

unsigned long hisi_totalmem(unsigned long *media_mem) {
    *media_mem = hisi_media_mem();
    if (is_cma_allocator())
        return kernel_mem();
    else
        return *media_mem + kernel_mem();
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
    close_sensor_fd = universal_close_sensor_fd;
    hal_cleanup = hisi_hal_cleanup;
    sensor_i2c_change_addr = universal_sensor_i2c_change_addr;
    if (chip_generation == HISI_V1) {
        open_sensor_fd = hisi_gen1_open_sensor_fd;
        sensor_read_register = xm_sensor_read_register;
        sensor_write_register = xm_sensor_write_register;
    } else if (chip_generation == HISI_V2 || chip_generation == HISI_V2A) {
        sensor_read_register = hisi_gen2_sensor_read_register;
        sensor_write_register = hisi_gen2_sensor_write_register;
        sensor_i2c_change_addr = hisi_gen2_sensor_i2c_change_addr;
    } else {
        sensor_read_register = hisi_sensor_read_register;
        sensor_write_register = hisi_sensor_write_register;
    }
    possible_i2c_addrs = hisi_possible_i2c_addrs;
    strcpy(short_manufacturer, "HI");
    hal_temperature = hisi_get_temp;
#ifndef STANDALONE_LIBRARY
    hal_detect_ethernet = hisi_ethdetect;
#endif
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

// T-sensor temperature record register 0
#define CV300_MISC_CTRL41 0x120300A4
// Temperature sensor (T-Sensor) control register
#define CV300_MISC_CTRL39 0x1203009C

// T-Sensor temperature record register 0
#define AV300_MISC_CTRL47 0x120300BC
// Temperature sensor (T-Sensor) control register
#define AV300_MISC_CTRL45 0x120300B4

static float hisi_get_temp() {
    float tempo;
    switch (chip_generation) {
    case HISI_V2:
        tempo = hisi_reg_temp(0x20270114, 8, 0x20270110, 0x60FA0000);
        tempo = ((tempo * 180) / 256) - 40;
        break;
    case HISI_V3:
        tempo =
            hisi_reg_temp(CV300_MISC_CTRL41, 16, CV300_MISC_CTRL39, 0x60FA0000);
        tempo = ((tempo - 125) / 806) * 165 - 40;
        break;
    case HISI_V4:
        tempo = hisi_reg_temp(0x120280BC, 16, 0x120280B4, 0xC3200000);
        tempo = ((tempo - 117) / 798) * 165 - 40;
        break;
    case HISI_V4A:
        tempo =
            hisi_reg_temp(AV300_MISC_CTRL47, 16, AV300_MISC_CTRL45, 0x60FA0000);
        tempo = ((tempo - 136) / 793 * 165) - 40;
        break;
    default:
        return NAN;
    }

    return tempo;
}

static const char *get_chip_V1() {
    uint32_t val;
    if (!mem_reg(0x2005008C, &val, OP_READ))
        goto err;

    switch ((val >> 8) & 0x7f) {
    case 0x10:
        return "3518CV100";
    case 0x57:
        return "3518EV100";
    default:
        val = 3;
        if (!mem_reg(0x20050088, &val, OP_WRITE))
            goto err;
        if (!mem_reg(0x20050088, &val, OP_READ))
            goto err;
        switch (val) {
        case 1:
            return "3516CV100";
        case 2:
            return "3518EV100";
        case 3:
            return "3518AV100";
        default:
            goto err;
        }
    }

err:
    return "unknown";
}

static const char *get_chip_V2A(uint8_t scsysid0) {
    switch (scsysid0) {
    case 0:
    case 1:
        // possibly 3 and 4 could be also valid AV100 revisions
        return "3516AV100";
    case 2:
        return "3516DV100";
    default:
        fprintf(stderr, "reserved value %#x", scsysid0);
        return "unknown";
    }
}

static const char *get_chip_V2(uint8_t scsysid0) {
    switch (scsysid0) {
    case 1:
        return "3516CV200";
    case 2:
        return "3518EV200";
    case 3:
        return "3518EV201";
    default:
        fprintf(stderr, "reserved value %#x", scsysid0);
        return "unknown";
    }
}

static const char *get_chip_V3A(uint8_t scsysid0) {
    switch (scsysid0) {
    case 0:
    case 1:
    case 2:
    case 0x11:
    case 0x12:
        return "3519V101";
    case 3:
        return "3559V100";
    case 4:
        return "3556V100";
    case 5:
    case 6:
    case 0x15:
    case 0x16:
        return "3516AV200";
    default:
        fprintf(stderr, "reserved value %#x", scsysid0);
        return "unknown";
    }
}

static const char *get_chip_V3(uint8_t scsysid0) {
    switch (scsysid0) {
    case 0:
        return "3516CV300";
    case 4:
        return "3516EV100";
    default:
        fprintf(stderr, "reserved value %#x", scsysid0);
        return "unknown";
    }
}

static const char *get_chip_NVR3516(uint8_t scsysid0) {
    switch (scsysid0) {
    case 0:
    case 1:
        return "3521DV100";
    case 2:
        return "3536CV100";
    case 3:
        return "3520DV400";
    default:
        fprintf(stderr, "reserved value %#x", scsysid0);
        return "unknown";
    }
}

static const char *get_hisi_chip_id(uint32_t family_id, uint8_t scsysid0) {
    switch (family_id) {
    case 0x3516A100:
        chip_generation = HISI_V2A;
        return get_chip_V2A(scsysid0);
    case 0x35190101:
        chip_generation = HISI_V3A;
        return get_chip_V3A(scsysid0);
    case 0x3516A300:
        chip_generation = HISI_V4A;
        return "3516AV300";
    case 0x3516C300:
        chip_generation = HISI_V3;
        return get_chip_V3(scsysid0);
    case 0x3516C500:
        chip_generation = HISI_V4A;
        return "3516CV500";
    case 0x3516D300:
        chip_generation = HISI_V4A;
        return "3516DV300";
    case 0x3516E200:
        chip_generation = HISI_V4;
        return "3516EV200";
    case 0x3516E300:
        chip_generation = HISI_V4;
        return "3516EV300";
    case 0x35180100:
        chip_generation = HISI_V1;
        return get_chip_V1();
    case 0x3518E200:
        chip_generation = HISI_V2;
        return get_chip_V2(scsysid0);
    case 0x3518E300:
        chip_generation = HISI_V4;
        return "3518EV300";
    case 0x3536D100:
        chip_generation = HISI_V2;
        return "3536DV100";
    case 0x3520D100:
        return "3520DV200";
    case 0x35210100:
        return "3521V100";
    case 0x3559A100:
        return "3559AV100";
    case 0xBDA9D100:
        return get_chip_NVR3516(scsysid0);
    case 0x72050200:
        // former 3516EV200
        chip_generation = HISI_V4;
        return "7205V200";
    case 0x72020300:
        // former 3518EV300
        chip_generation = HISI_V4;
        return "7202V300";
    case 0x72050300:
        // former 3516EV300
        chip_generation = HISI_V4;
        return "7205V300";
    case 0x76050100:
        // former 3516DV200
        chip_generation = HISI_V4;
        return "7605V100";
    default:
        fprintf(stderr, "Got unexpected ID 0x%x for HiSilicon\n", family_id);
        return "unknown";
    }
}

#define SCSYSID0 0xEE0

bool hisi_detect_cpu(uint32_t SC_CTRL_base) {
    uint32_t SCSYSID[4] = {0};

    uint32_t family_id = 0;
    for (int i = 0; i < 4; i++) {
        if (!mem_reg(SC_CTRL_base + SCSYSID0 + i * sizeof(uint32_t),
                     (uint32_t *)&SCSYSID[i], OP_READ))
            return false;
        if (i == 0 && (SCSYSID[i] >> 16 & 0xff) != 0) {
            // special case for new platforms
            family_id = SCSYSID[i];
            break;
        }
        family_id |= (SCSYSID[i] & 0xff) << i * 8;
    }

    strncpy(chip_id, get_hisi_chip_id(family_id, SCSYSID[0] >> 24),
            sizeof(chip_id));

    if (*chip_id == '7')
        strcpy(chip_manufacturer, VENDOR_GOKE);
    else
        strcpy(chip_manufacturer, VENDOR_HISI);

    return true;
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
} lvds_sync_mode_t;

typedef enum {
    LVDS_ENDIAN_LITTLE = 0x0,
    LVDS_ENDIAN_BIG = 0x1,
} lvds_bit_endian_t;

const uint32_t CV300_LVDS0_CTRL_ADDR = CV300_MIPI_BASE + 0x1304;
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
    case HISI_V2A:
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
        ADD_PARAM(param, "LVDS_ENDIAN_LITTLE")
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
                cJSON *j_lanes = cJSON_AddArrayToObject(j_inner, "lane-id");
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
                ADD_PARAM_NUM("lvds-wdr-en", wdr.lvds_wdr_en);
                ADD_PARAM_NUM("lvds-wdr-mode", wdr.lvds_wdr_mode);
                ADD_PARAM_NUM("lvds-wdr-num", wdr.lvds_wdr_num);
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
                    ADD_PARAM("raw-data-type", raw);

                if (is_lvds) {
                    if (lvds0_ctrl.lvds_sync_mode == LVDS_SYNC_MODE_SOF)
                        ADD_PARAM("sync-mode", "LVDS_SYNC_MODE_SOF")
                    else
                        ADD_PARAM("sync-mode", "LVDS_SYNC_MODE_SAV");

                    lvds_code_set(j_inner, "data-endian",
                                  lvds0_ctrl.lvds_pix_big_endian);
                    lvds_code_set(j_inner, "sync-code-endian",
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
    case HISI_V2A:
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
            ADD_PARAM("vi-state", "down");

        return !reg.enable;
    }

    return false;
}

static void determine_sensor_data_type(cJSON *j_inner) {
    switch (chip_generation) {
    case HISI_V1:
        return hisi_cv100_sensor_data(j_inner);
    case HISI_V2A:
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
    if (hisi_vi_is_not_running(ctx->j_sensor))
        return;

    determine_sensor_data_type(ctx->j_sensor);
    determine_sensor_clock(ctx->j_sensor);
}

#define CV100_FMC_BASE 0x10010000

const uint32_t CV100_GLOBAL_CONFIG = 0x0100;
struct CV100_GLOBAL_CONFIG {
    unsigned int mode : 1;
    bool wp_en : 1;
    unsigned int flash_addr_mode : 1;
};

#define CV200_FMC_BASE 0x10010000
#define CV300_FMC_BASE 0x10000000
#define EV300_FMC_BASE 0x10000000

const uint32_t CV200_FMC_CFG = CV200_FMC_BASE + 0;
const uint32_t CV300_FMC_CFG = CV300_FMC_BASE + 0;
const uint32_t EV300_FMC_CFG = EV300_FMC_BASE + 0;
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
}

void hisi_detect_fmc() {
    const char *mode = NULL;
    switch (chip_generation) {
    case HISI_V1: {
        struct CV100_GLOBAL_CONFIG val;
        if (mem_reg(CV100_GLOBAL_CONFIG, (uint32_t *)&val, OP_READ))
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
    if (mode)
        printf("    addr-mode: %s\n", mode);
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

typedef unsigned int combo_dev_t;

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

#define MIPI_LANE_NUM 4
#define LVDS_LANE_NUM 4
#define WDR_VC_NUM 2 // and 4 for CV500
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
    data_type_t input_data_type;
    mipi_wdr_mode_t wdr_mode;
    short lane_id[MIPI_LANE_NUM];

    union {
        short data_type[WDR_VC_NUM];
    };
} mipi_dev_attr_t;

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
    combo_dev_t devno;
    input_mode_t input_mode;
    mipi_data_rate_t data_rate;
    img_rect_t img_rect;

    union {
        mipi_dev_attr_t mipi_attr;
        lvds_dev_attr_t lvds_attr;
    };
} combo_dev_attr_t;

size_t hisi_sizeof_combo_dev_attr() { return sizeof(combo_dev_attr_t); }

void hisi_dump_combo_dev_attr(void *ptr, unsigned int cmd) {
    combo_dev_attr_t *attr = ptr;

    printf("combo_dev_attr_t SENSOR_ATTR = {\n"
           "\t.devno = %d,\n"
           "\t.input_mode = %s,\n"
           "\t.data_rate = %s,\n"
           "\t.img_rect = {%d, %d, %d, %d},\n"
           "\t{\n",
           attr->devno, input_mode_t_str(attr->input_mode),
           mipi_data_rate_t_str(attr->data_rate), attr->img_rect.x,
           attr->img_rect.y, attr->img_rect.width, attr->img_rect.height);
    if (attr->input_mode == INPUT_MODE_MIPI) {
        printf("\t\t.mipi_attr =\n"
               "\t\t{\n"
               "\t\t\t%s,\n"
               "\t\t\t%s,\n"
               "\t\t\t{%d, %d, %d, %d}\n"
               "\t\t}\n",
               data_type_t_str(attr->mipi_attr.input_data_type),
               mipi_wdr_mode_t_str(attr->mipi_attr.wdr_mode),
               attr->mipi_attr.lane_id[0], attr->mipi_attr.lane_id[1],
               attr->mipi_attr.lane_id[2], attr->mipi_attr.lane_id[3]);
    } else if (attr->input_mode == INPUT_MODE_LVDS) {
    }
    printf("\t}\n};\n");
}

#endif
