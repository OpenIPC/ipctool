#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <sys/resource.h>
#include <unistd.h>

#include "chipid.h"
#include "hal_common.h"
#include "hisi/ethernet.h"
#include "hisi/ispreg.h"
#include "ram.h"
#include "tools.h"

#ifndef STANDALONE_LIBRARY
#include "cjson/cJSON.h"
#endif

static unsigned char sony_addrs[] = {0x34, 0};
static unsigned char soi_addrs[] = {0x80, 0x60, 0};
static unsigned char onsemi_addrs[] = {0x20, 0x30, 0};
static unsigned char ssens_addrs[] = {0x60, 0};
static unsigned char omni_addrs[] = {0x60, 0x6c, 0x42, 0};
static unsigned char gc_addrs[] = {0x6e, 0x52, 0};
static unsigned char superpix_addrs[] = {0x79, 0};

sensor_addr_t hisi_possible_i2c_addrs[] = {
    {SENSOR_SONY, sony_addrs},         {SENSOR_SOI, soi_addrs},
    {SENSOR_ONSEMI, onsemi_addrs},     {SENSOR_SMARTSENS, ssens_addrs},
    {SENSOR_OMNIVISION, omni_addrs},   {SENSOR_GALAXYCORE, gc_addrs},
    {SENSOR_SUPERPIX, superpix_addrs}, {0, NULL}};

static float hisi_get_temp();

static int hisi_open_i2c_fd() {
    int adapter_nr = 0; /* probably dynamically determined */
    char filename[FILENAME_MAX];

    snprintf(filename, sizeof(filename), "/dev/i2c-%d", adapter_nr);

    return universal_open_sensor_fd(filename);
}

static int hisi_open_spi_fd() {
    unsigned int value;
    int ret;
    int adapter_nr = 0; /* probably dynamically determined */
    char filename[FILENAME_MAX];

    snprintf(filename, sizeof(filename), "/dev/spidev0.%d", adapter_nr);

    int fd = universal_open_sensor_fd(filename);

    value = SPI_MODE_3 | SPI_LSB_FIRST;
    ret = ioctl(fd, SPI_IOC_WR_MODE, &value);
    if (ret < 0) {
        fprintf(stderr, "ioctl SPI_IOC_WR_MODE err, value = %d ret = %d\n",
                value, ret);
        return ret;
    }

    value = 8;
    ret = ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &value);
    if (ret < 0) {
        fprintf(stderr,
                "ioctl SPI_IOC_WR_BITS_PER_WORD err, value = %d ret = %d\n",
                value, ret);
        return ret;
    }

    value = 2000000;
    ret = ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &value);
    if (ret < 0) {
        fprintf(stderr,
                "ioctl SPI_IOC_WR_MAX_SPEED_HZ err, value = %d ret = %d\n",
                value, ret);
        return ret;
    }

    return fd;
}

static int hisi_gen1_open_i2c_sensor_fd() {
    return universal_open_sensor_fd("/dev/hi_i2c");
}

static int hisi_gen1_open_spi_sensor_fd() {
    return universal_open_sensor_fd("/dev/ssp");
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

    for (size_t cur_addr = reg_addr; cur_addr <= reg_addr_end; cur_addr += 1) {
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
    reg_addr = sony_i2c_to_spi(reg_addr);
    unsigned int data = (unsigned int)(((reg_addr & 0xffff) << 8));
    int ret = ioctl(fd, SSP_READ_ALT, &data);
    return data & 0xff;
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

#define CV300_MUX_BASE 0x12040000

#define CV300_MUX30_ADDR CV300_MUX_BASE + 0x0030
#define CV300_MUX2C_ADDR CV300_MUX_BASE + 0x002c
#define CV300_MUX14_ADDR CV300_MUX_BASE + 0x0038
#define CV300_MUX15_ADDR CV300_MUX_BASE + 0x003c

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

#ifndef STANDALONE_LIBRARY
static void get_hisi_sdk(cJSON *j_inner) {
    char buf[1024];

    if (get_regex_line_from_file("/proc/umap/sys", "Version: \\[(.+)\\]", buf,
                                 sizeof(buf))) {
        char *ptr = strchr(buf, ']');
        char *build = strchr(buf, '[');
        if (!ptr || !build)
            return;
        *ptr++ = ' ';
        *ptr++ = '(';
        strcpy(ptr, build + 1);
        strcat(ptr, ")");
        ADD_PARAM("sdk", buf);
    }
}
#endif

void setup_hal_hisi() {
    disable_printk();
    if (chip_generation == HISI_V3)
        v3_ensure_sensor_enabled();
    else if (chip_generation == HISI_V4)
        v4_ensure_sensor_enabled();

    open_i2c_sensor_fd = hisi_open_i2c_fd;
    open_spi_sensor_fd = hisi_open_spi_fd;
    hal_cleanup = hisi_hal_cleanup;
    if (chip_generation == HISI_V1) {
        open_i2c_sensor_fd = hisi_gen1_open_i2c_sensor_fd;
        open_spi_sensor_fd = hisi_gen1_open_spi_sensor_fd;
        i2c_read_register = xm_sensor_read_register;
        i2c_write_register = xm_sensor_write_register;
        spi_read_register = sony_ssp_read_register;
    } else if (chip_generation == HISI_V2 || chip_generation == HISI_V2A) {
        i2c_read_register = hisi_gen2_sensor_read_register;
        i2c_write_register = hisi_gen2_sensor_write_register;
        i2c_change_addr = hisi_gen2_sensor_i2c_change_addr;
    } else {
        i2c_read_register = hisi_sensor_read_register;
        i2c_write_register = hisi_sensor_write_register;
    }
    possible_i2c_addrs = hisi_possible_i2c_addrs;
    hal_temperature = hisi_get_temp;
#ifndef STANDALONE_LIBRARY
    hal_detect_ethernet = hisi_ethdetect;
    hal_totalmem = hisi_totalmem;
    hal_fmc_mode = hisi_detect_fmc;
    hal_chip_properties = hisi_chip_properties;
    hal_firmware_props = get_hisi_sdk;
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
    case 0x3516D200:
        chip_generation = HISI_V4;
        return "3516DV200";
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
    case 0x3536C100:
        chip_generation = HISI_3536C;
        return "3536CV100";
    case 0x3536D100:
        chip_generation = HISI_3536D;
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
    case 0x72050210:
        // new chip in the line?
        chip_generation = HISI_V4;
        return "7205V210";
    default:
        fprintf(stderr, "Got unexpected ID 0x%x for HiSilicon\n", family_id);
        return "unknown";
    }
}

#define SCSYSID0 0xEE0

bool hisi_detect_cpu(char *chip_name, uint32_t SC_CTRL_base) {
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

    strcpy(chip_name, get_hisi_chip_id(family_id, SCSYSID[0] >> 24));

    return true;
}
