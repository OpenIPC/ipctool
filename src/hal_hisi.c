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
    mesg[0].tx_buf = (__u64)(__u32)&tx_buf;
    mesg[0].len = 3;
    mesg[0].rx_buf = (__u64)(__u32)&rx_buf;
    mesg[0].cs_change = 1;

    ret = ioctl(fd, SPI_IOC_MESSAGE(1), mesg);
    if (ret < 0) {
        printf("SPI_IOC_MESSAGE error \n");
        return -1;
    }

    return rx_buf[2];
}

uint32_t rounded_num(uint32_t n) {
    int i;
    for (i = 0; n; i++) {
        n /= 2;
    }
    return 1 << i;
}

static bool hisi_mmz_total() {
    char buf[256];

    if (!get_regex_line_from_file("/proc/media-mem", "total size=([0-9]+)KB",
                                  buf, sizeof(buf))) {
        return false;
    }
    unsigned long media_mem = strtoul(buf, NULL, 10);
    uint32_t total_mem = (media_mem + kernel_mem()) / 1024;
    sprintf(ram_specific + strlen(ram_specific), "  total: %uM\n",
            rounded_num(total_mem));
    sprintf(ram_specific + strlen(ram_specific), "  media: %luM\n",
            media_mem / 1024);
    return true;
}

static char printk_state[16];
#define PRINTK_FILE "/proc/sys/kernel/printk"
void disable_printk() {
    if (*printk_state)
        return;

    FILE *fp = fopen(PRINTK_FILE, "rw+");
    if (!fp)
        return;
    fgets(printk_state, sizeof(printk_state) - 1, fp);
    // We cannot use rewind() here
    fclose(fp);
    fp = fopen(PRINTK_FILE, "w");
    fprintf(fp, "0 0 0 0\n");
    fclose(fp);
}

void restore_printk() {
    if (!*printk_state)
        return;

    FILE *fp = fopen(PRINTK_FILE, "w");
    fprintf(fp, "%s", printk_state);
    fclose(fp);
}

static void hisi_hal_cleanup() { restore_printk(); }

void setup_hal_hisi() {
    disable_printk();

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
    hisi_mmz_total();
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

const unsigned int CV100_PERI_CRG12_ADRR = 0x20030030;
const char *hisi_cv100_get_sensor_clock() {
    struct CV100_PERI_CRG12 crg12;
    int res = mem_reg(CV100_PERI_CRG12_ADRR, (uint32_t *)&crg12, OP_READ);
    if (res) {
        return cv100_sensor_clksel(crg12.sense_cksel);
    }
    return NULL;
}

const char *hisi_cv100_get_sensor_data_type() { return "DC"; }

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
const char *hisi_cv200_get_sensor_data_type() {
    struct CV200_MISC_CTRL1 ctrl1;
    bool res = mem_reg(CV200_MISC_CTRL1_ADDR, (uint32_t *)&ctrl1, OP_READ);
    // TODO: consider valid is ISP is running
    if (res && *(uint32_t *)&ctrl1) {
        switch (ctrl1.mipi_phy_mode) {
        case CV200_PHY_MIPI_MODE:
            return "MIPI";
        case CV200_PHY_CMOS_MODE:
            return "DC";
        case CV200_PHY_LVDS_MODE:
            return "LVDS";
        case CV200_PHY_BYPASS:
            return "BYPASS";
        default:
            return NULL;
        }
    }
    return NULL;
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

const unsigned int CV300_MISC_CTRL0_ADDR = 0x12030000;
const char *hisi_cv300_get_sensor_data_type() {
    struct CV300_MISC_CTRL0 ctrl0;
    bool res = mem_reg(CV300_MISC_CTRL0_ADDR, (uint32_t *)&ctrl0, OP_READ);
    // consider 0 as invalid value (system can just reseted)
    if (res && *(uint32_t *)&ctrl0) {
        switch (ctrl0.mipi_phy_mode) {
        case CV300_PHY_MIPI_MODE:
            return "MIPI";
        case CV300_PHY_CMOS_MODE:
            return "DC";
        case CV300_PHY_LVDS_MODE:
            return "LVDS";
        default:
            return NULL;
        }
    }
    return NULL;
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

const unsigned int CV200_PERI_CRG11_ADRR = 0x2003002c;
const char *hisi_cv200_get_sensor_clock() {
    struct CV200_PERI_CRG11 crg11;
    int res = mem_reg(CV200_PERI_CRG11_ADRR, (uint32_t *)&crg11, OP_READ);
    // consider sensor clock value only when it's enabled
    if (res && crg11.sensor_cken) {
        return cv200_cv300_map_sensor_clksel(crg11.sensor_cksel);
    }
    return NULL;
}

const unsigned int CV300_PERI_CRG11_ADRR = 0x1201002c;
const char *hisi_cv300_get_sensor_clock() {
    struct CV300_PERI_CRG11 crg11;
    int res = mem_reg(CV300_PERI_CRG11_ADRR, (uint32_t *)&crg11, OP_READ);
    // consider sensor clock value only when it's enabled
    if (res && crg11.sensor_cken) {
        return cv200_cv300_map_sensor_clksel(crg11.sensor_clksel);
    }
    return NULL;
}

struct EV300_PERI_CRG60 {
    bool sensor0_cken : 1;
    unsigned int sensor0_srst_req : 1;
    unsigned int sensor0_cksel : 3;
    bool sensor0_ctrl_cken : 1;
    unsigned int sensor0_ctrl_srst_req : 1;
};

enum EV300_MIPI_PHY {
    EV300_PHY_MIPI_MODE = 0,
    EV300_PHY_LVDS_MODE,
    EV300_PHY_RESERVED0,
    EV300_PHY_RESERVED1
};

struct EV300_MISC_CTRL6 {
    enum EV300_MIPI_PHY mipirx0_work_mode : 2;
};

const unsigned int EV300_MISC_CTRL6_ADDR = 0x12028018;
const char *hisi_ev300_get_sensor_data_type() {
    struct EV300_MISC_CTRL6 ctrl6;
    bool res = mem_reg(EV300_MISC_CTRL6_ADDR, (uint32_t *)&ctrl6, OP_READ);
    if (res) {
        switch (ctrl6.mipirx0_work_mode) {
        case EV300_PHY_MIPI_MODE:
            return "MIPI";
        case EV300_PHY_LVDS_MODE:
            return "LVDS";
        default:
            return NULL;
        }
    }

    return NULL;
}

const unsigned int EV300_PERI_CRG60_ADRR = 0x120100F0;
const char *hisi_ev300_get_sensor_clock() {
    struct EV300_PERI_CRG60 crg60;
    int res = mem_reg(EV300_PERI_CRG60_ADRR, (uint32_t *)&crg60, OP_READ);
    // consider sensor clock value only when it's enabled
    if (res && crg60.sensor0_cken) {
        switch (crg60.sensor0_cksel) {
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
        }
    }
    return NULL;
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

int hisi_get_temp() {
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
        return EXIT_FAILURE;
    }
    printf("%.2f\n", tempo);

    return EXIT_SUCCESS;
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

void cv300_enum_lanes() {
    uint32_t addr = 0x11301320;
    uint32_t end_addr = 0x1130141C;

    for (;;) {
    }
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

/*
ev300 = 0x1100_0000 + 0x1000 + PT_N x 0x100;
cv300 = 0x1138_0000 + 0x0100;
cv200 = 0x1100_0000 + 0x1000 + PT_N x 0x100;
cv100 =
*/

struct PT_INTF_MOD {
    unsigned int mode : 1;
    unsigned int res : 30;
    bool enable : 1;
};

const bool hisi_vi_is_not_running() {
    uint32_t addr = 0, PT_N = 0;
    switch (chip_generation) {
    case HISI_V1:
    case HISI_V2:
        addr = 0x20580000 + 0x0100;
        break;
    case HISI_V3:
        addr = 0x11380000 + 0x0100;
        break;
    case HISI_V4:
        addr = 0x11000000 + 0x1000 + PT_N * 0x100;
        break;
    default:
        return false;
    }
    struct PT_INTF_MOD reg;
    if (mem_reg(addr, (uint32_t *)&reg, OP_READ)) {
        // if (!reg.enable) // viState: down

        return !reg.enable;
    }

    return false;
}

static const char *get_sensor_data_type() {
    switch (chip_generation) {
    case HISI_V1:
        return hisi_cv100_get_sensor_data_type();
    case HISI_V2:
        return hisi_cv200_get_sensor_data_type();
    case HISI_V3:
        return hisi_cv300_get_sensor_data_type();
    case HISI_V4:
        return hisi_ev300_get_sensor_data_type();
    default:
        return NULL;
    }
}

static const char *get_sensor_clock() {
    switch (chip_generation) {
    case HISI_V1:
        return hisi_cv100_get_sensor_clock();
    case HISI_V2:
        return hisi_cv200_get_sensor_clock();
    case HISI_V3:
        return hisi_cv300_get_sensor_clock();
    case HISI_V4:
        return hisi_ev300_get_sensor_clock();
    default:
        return NULL;
    }
}

void hisi_vi_information(cJSON *j_root) {
    if (hisi_vi_is_not_running())
        return;

    const char *data_type = get_sensor_data_type();
    if (data_type) {
        cJSON *j_inner = cJSON_CreateObject();
        cJSON_AddItemToObject(j_root, "data", j_inner);
        ADD_PARAM("type", data_type);
    }

    const char *sensor_clock = get_sensor_clock();
    if (sensor_clock) {
        cJSON *j_inner = j_root;
        ADD_PARAM("clock", sensor_clock);
    }
}
