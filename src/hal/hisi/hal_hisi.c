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
#include "hal/common.h"
#include "hal/hisi/ethernet.h"
#include "hal/hisi/ispreg.h"
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
static unsigned char gc_addrs[] = {0x6e, 0x52, 0x42, 0x78, 0};
static unsigned char superpix_addrs[] = {0x79, 0};
static unsigned char tp_addrs[] = {0x88, 0};
static unsigned char imagedesign_addrs[] = {0x60, 0};
/* 0x6c: the VS4388 of a Hi3516CV610 camera, whose vendor firmware addresses
 * it there (I2C_SLAVE_FORCE 0x36) and not at 0x20. */
static unsigned char visemi_addrs[] = {0x20, 0x6c, 0};
static unsigned char cvsens_addrs[] = {0x6a, 0};

static sensor_addr_t my_possible_i2c_addrs[] = {
    {SENSOR_SONY, sony_addrs},         {SENSOR_SOI, soi_addrs},
    {SENSOR_ONSEMI, onsemi_addrs},     {SENSOR_SMARTSENS, ssens_addrs},
    {SENSOR_OMNIVISION, omni_addrs},   {SENSOR_GALAXYCORE, gc_addrs},
    {SENSOR_SUPERPIX, superpix_addrs}, {SENSOR_TECHPOINT, tp_addrs},
    {SENSOR_IMAGEDESIGN, imagedesign_addrs}, {SENSOR_VISEMI, visemi_addrs}, 
    {SENSOR_CVSENS, cvsens_addrs}, {0, NULL}};

static float hisi_get_temp();

static int hisi_open_i2c_fd() {
    char filename[FILENAME_MAX];

    snprintf(filename, sizeof(filename), "/dev/i2c-%d", i2c_adapter_nr);

    return universal_open_sensor_fd(filename);
}

static int hisi_open_spi_fd() {
    unsigned int value;
    int ret;
    int adapter_nr = 0; /* probably dynamically determined */
    char filename[FILENAME_MAX];

    snprintf(filename, sizeof(filename), "/dev/spidev0.%d", adapter_nr);

    int fd = universal_open_sensor_fd(filename);
    /* Cameras without spidev are the common case, not an error worth three
     * lines on stderr: every ioctl below was being issued on -1. */
    if (fd < 0)
        return -1;

    value = SPI_MODE_3 | SPI_LSB_FIRST;
    ret = ioctl(fd, SPI_IOC_WR_MODE, &value);
    if (ret < 0) {
        fprintf(stderr, "ioctl SPI_IOC_WR_MODE err, value = %d ret = %d\n",
                value, ret);
        close(fd);
        return ret;
    }

    value = 8;
    ret = ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &value);
    if (ret < 0) {
        fprintf(stderr,
                "ioctl SPI_IOC_WR_BITS_PER_WORD err, value = %d ret = %d\n",
                value, ret);
        close(fd);
        return ret;
    }

    value = 2000000;
    ret = ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &value);
    if (ret < 0) {
        fprintf(stderr,
                "ioctl SPI_IOC_WR_MAX_SPEED_HZ err, value = %d ret = %d\n",
                value, ret);
        close(fd);
        return ret;
    }

    return fd;
}

#ifdef IPCHW_HISI_V1
static int hisi_gen1_open_i2c_sensor_fd() {
    return universal_open_sensor_fd("/dev/hi_i2c");
}

static int hisi_gen1_open_spi_sensor_fd() {
    return universal_open_sensor_fd("/dev/ssp");
}

#endif /* IPCHW_HISI_V1 */

#define I2C_16BIT_REG 0x0709  /* 16BIT REG WIDTH */
#define I2C_16BIT_DATA 0x070a /* 16BIT DATA WIDTH */

#if defined(IPCHW_HISI_V2) || defined(IPCHW_HISI_V2A)
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

#endif /* IPCHW_HISI_V2 || IPCHW_HISI_V2A */

#ifdef IPCHW_HISI_V1
int hisi_gen1_sensor_write_register(int fd, unsigned char i2c_addr,
                             unsigned int reg_addr, unsigned int reg_width,
                             unsigned int data, unsigned int data_width) {
    int ret;
    I2C_DATA_S i2c_data;

    i2c_data.dev_addr = i2c_addr;
    i2c_data.reg_addr = reg_addr;
    i2c_data.addr_byte_num = reg_width;
    i2c_data.data = data;
    i2c_data.data_byte_num = data_width;

    ret = ioctl(fd, CMD_I2C_WRITE, &i2c_data);

    if (ret) {
        printf("i2c write failed!\n");
        return -1;
    }

    return 0;
}

#endif /* IPCHW_HISI_V1 */

#if defined(IPCHW_HISI_V2) || defined(IPCHW_HISI_V2A)
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

#endif /* IPCHW_HISI_V2 || IPCHW_HISI_V2A */

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

#ifdef IPCHW_HISI_V1
int hisi_gen1_sensor_read_register(int fd, unsigned char i2c_addr,
                            unsigned int reg_addr, unsigned int reg_width,
                            unsigned int data_width) {
    int ret;
    I2C_DATA_S i2c_data;

    i2c_data.dev_addr = i2c_addr;
    i2c_data.reg_addr = reg_addr;
    i2c_data.addr_byte_num = reg_width;
    i2c_data.data_byte_num = data_width;

    ret = ioctl(fd, CMD_I2C_READ, &i2c_data);
    if (ret) {
        printf("i2c read failed!\n");
        return -1;
    }

    return i2c_data.data;
}

#endif /* IPCHW_HISI_V1 */

#if defined(IPCHW_HISI_V2) || defined(IPCHW_HISI_V2A)
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

#endif /* IPCHW_HISI_V2 || IPCHW_HISI_V2A */

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
#ifdef IPCHW_HISI_V1
int sony_ssp_read_register(int fd, unsigned char i2c_addr,
                           unsigned int reg_addr, unsigned int reg_width,
                           unsigned int data_width) {
    reg_addr = sony_i2c_to_spi(reg_addr);
    unsigned int data = (unsigned int)(((reg_addr & 0xffff) << 8));
    int ret = ioctl(fd, SSP_READ_ALT, &data);
    return data & 0xff;
}

#endif /* IPCHW_HISI_V1 */

static unsigned long hisi_media_mem() {
    char buf[256];

    if (!line_from_file("/proc/media-mem", "total size=([0-9]+)KB", buf,
                        sizeof(buf))) {
        return 0;
    }
    return strtoul(buf, NULL, 10);
}

static bool is_cma_allocator() {
    char buf[64];
    if (line_from_file("/proc/cmdline", "mmz_allocator=(\\w+)", buf,
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

#ifdef IPCHW_HISI_V3
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

#endif /* IPCHW_HISI_V3 */

#if defined(IPCHW_HISI_V4) || defined(IPCHW_HISI_V5) ||                     \
    defined(IPCHW_HISI_GOKE7206)
/* Ungating the sensor's clock for a probe, and putting it back afterwards.
 *
 * NOT a snapshot -- an ENTITLEMENT. What this used to keep was the register as
 * some earlier probe found it, plus a bool that was set the first time a probe
 * found the clock gated and was never cleared again. So a later probe that
 * found the clock already RUNNING wrote nothing, took nothing, and still fired
 * at hal_cleanup() -- restoring a word saved in a different era, from before
 * the consumer started its pipeline.
 *
 * Measured, on a hi3516ev300 + IMX335 streaming 2592x1944: 0x120100F0 went
 * 0x11 -> 0x10 underneath a live pipeline, and a sensor with no clock answers
 * no i2c, so the kernel driver's register writes failed -- 908 of
 * `[sensor_i2c_write 112] i2c_master_send error, ret=-5`. The picture froze
 * with the OSD still drawn on it and only a full pipeline rebuild recovered
 * it. Writing the single bit back stopped the errors dead and restored the
 * picture (OpenIPC/firmware#2439).
 *
 * Three rules, and the third is the one that fixes it:
 *
 *   1. We write this register only to ungate a clock we found gated, or to
 *      undo exactly that ungate while the register still holds the precise
 *      word we left in it.
 *   2. One arm entitles at most one undo, spent whether or not it wrote.
 *   3. An arm that finds the register already fit for a probe takes no
 *      entitlement at all. A clock we did not have to turn on is not ours to
 *      turn off.
 *
 * Rule 3 is load-bearing and rule 1 cannot replace it: the word we write is
 * 0x11, and 0x11 is also what the vendor SDK writes, so "the register still
 * matches what I wrote" cannot by itself tell our own ungate from the SDK's.
 *
 * One race is left and cannot be closed from this register: if the consumer
 * enables the clock BETWEEN our arm and our restore, writing the identical
 * word, we still gate it. That window is one probe rather than the hours this
 * replaces, and nothing readable here distinguishes the two. */
typedef struct {
    uint32_t found; /* the word before we touched it */
    uint32_t wrote; /* the word we left, so we can tell if it still stands */
    bool owed;      /* an undo is outstanding */
} sensor_crg_t;

/* sel_mask/sel_safe: a clock-select field and the value to put in it when
 * we ungate a clock whose select still holds its reset value (0). Only a
 * clock we find gated gets one: a running clock was set up by somebody, at
 * whatever rate they chose. Pass 0/0 where the register has no such field. */
static void sensor_crg_enable(sensor_crg_t *st, uint32_t addr, uint32_t cken,
                              uint32_t srst, uint32_t sel_mask,
                              uint32_t sel_safe) {
    uint32_t cur;

    /* A read that failed taught us nothing. Drop any entitlement rather than
     * carry it forward: we can no longer say what the register holds, and of
     * the two ways to be wrong, leaving a clock running costs microamps while
     * gating one takes a streaming camera off the air. */
    if (!mem_reg(addr, &cur, OP_READ)) {
        st->owed = false;
        return;
    }

    uint32_t want = (cur | cken) & ~srst;
    if (sel_mask && !(cur & cken) && !(cur & sel_mask))
        want = (want & ~sel_mask) | sel_safe;
    if (want == cur) {
        /* Already fit for a probe, so this arm takes nothing -- and drops
         * anything an earlier one held.
         *
         * UNCONDITIONALLY, and that is the whole point. It is tempting to keep
         * the entitlement when the register still holds the word we wrote, on
         * the grounds that this is our own ungate being re-armed mid-sweep.
         * That was tried and it put the original bug straight back: the word
         * we write is 0x11 and 0x11 is also what the vendor SDK writes, so an
         * arm at boot that ungated a gated clock, followed by the consumer
         * starting its pipeline, followed by a probe half an hour later, looks
         * from this register exactly like our own ungate still standing. The
         * cleanup then handed a streaming camera's clock back to the gated
         * state. It reached the field before it was caught.
         *
         * The cost is that a probe which ungated a gated clock and then armed
         * again may leave it running. That is deliberate: of the two ways to
         * be wrong, a clock left on costs microamps and a clock taken away
         * costs the picture. */
        st->owed = false;
        return;
    }

    st->found = cur;
    st->wrote = want;
    /* Passing the saved word straight to mem_reg() is safe -- OP_WRITE only
     * reads through the pointer -- and the entitlement is taken only if the
     * write landed, because a write that failed changed nothing to undo. */
    if (mem_reg(addr, &st->wrote, OP_WRITE))
        st->owed = true;
}

static void sensor_crg_restore(sensor_crg_t *st, uint32_t addr) {
    uint32_t cur;

    if (!st->owed)
        return;
    st->owed = false; /* rule 2: spent even if the checks below decline */

    /* rule 1: somebody else owns it now -- a reprogrammed clock select, a
     * reset asserted, anything at all. Leave it to them. */
    if (!mem_reg(addr, &cur, OP_READ) || cur != st->wrote)
        return;

    mem_reg(addr, &st->found, OP_WRITE);
}
#endif /* V4 || V5 || GOKE7206 */

#ifdef IPCHW_HISI_GOKE7206
/* GK7206V1x: sensor 0's clock enable and reset are bits 0 and 1 of CRG 0xF0,
 * where the EV300 has them (the SDK's sys_hal_set_sensor_cken and
 * sys_hal_sensor_reset write bit 8n and 8n+1 there for sensor n). With the
 * clock gated, as it is until a streamer starts, the sensor does not answer
 * on I2C and ipctool reported none. */
static sensor_crg_t gk7206_crg;

static void gk7206_ensure_sensor_enabled() {
    sensor_crg_enable(&gk7206_crg, EV300_PERI_CRG60_ADDR,
                      EV300_PERI_CRG60_SENSOR0_CKEN,
                      EV300_PERI_CRG60_SENSOR0_SRST, 0, 0);
}

static void gk7206_ensure_sensor_restored() {
    sensor_crg_restore(&gk7206_crg, EV300_PERI_CRG60_ADDR);
}
#endif /* IPCHW_HISI_GOKE7206 */

#ifdef IPCHW_HISI_V4
static sensor_crg_t v4_crg;

static void v4_ensure_sensor_enabled() {
    sensor_crg_enable(&v4_crg, EV300_PERI_CRG60_ADDR,
                      EV300_PERI_CRG60_SENSOR0_CKEN,
                      EV300_PERI_CRG60_SENSOR0_SRST, 0, 0);
}

static void v4_ensure_sensor_restored() {
    sensor_crg_restore(&v4_crg, EV300_PERI_CRG60_ADDR);
}

#endif /* IPCHW_HISI_V4 */

#ifdef IPCHW_HISI_V5
/* Two registers, one per sensor lane, each entitled independently. */
static const uint32_t ot_crg_addr[2] = {
    CV610_PERI_CRG8464_ADDR,
    CV610_PERI_CRG8472_ADDR,
};
static sensor_crg_t ot_crg[2];

/* Hi3519DV500/Hi3516DV500: the SENSOR1 connector of the reference design is
 * controlled over I2C5, whose pads are SPI1_SCLK (iocfg_reg109) and SPI1_SDO
 * (iocfg_reg110) -- function 0 GPIO1_0/GPIO1_1, 1 SPI1, 2 I2C5 (data sheet
 * pin table). The vendor sys_config.ko only muxes them when built with
 * I2C_EX_EN, which the stock SDK is not, so on a stock board /dev/i2c-5
 * exists, every transfer on it times out, and a sensor on SENSOR1 is
 * invisible. Measured on a Taobao DV500 kit with an OS08A20 there.
 *
 * Same rules as the clock above. We switch a pad to I2C5 only while it is an
 * unclaimed GPIO input -- never one in SPI1 mode, never a GPIO the board
 * drives as an output -- and put back exactly what we found, and only if the
 * pad still holds what we left in it. */
#define DV500_IOCFG_SPI1_SCLK 0x0EFF0024u
#define DV500_IOCFG_SPI1_SDO 0x0EFF0028u
#define DV500_GPIO1_DIR 0x11091400u /* PL061 GPIODIR, bank 1 */
#define DV500_IOCFG_FUNC_MASK 0xFu
#define DV500_IOCFG_FUNC_GPIO 0x0u
#define DV500_IOCFG_FUNC_I2C5 0x2u

static const uint32_t dv500_i2c5_pad[2] = {
    DV500_IOCFG_SPI1_SCLK, /* GPIO1_0 */
    DV500_IOCFG_SPI1_SDO,  /* GPIO1_1 */
};
static sensor_crg_t dv500_i2c5[2];

static bool is_dv500(void) { return strstr(chip_name, "DV500") != NULL; }

static void dv500_i2c5_enable(void) {
    uint32_t cur[2], dir;

    if (!mem_reg(dv500_i2c5_pad[0], &cur[0], OP_READ) ||
        !mem_reg(dv500_i2c5_pad[1], &cur[1], OP_READ) ||
        !mem_reg(DV500_GPIO1_DIR, &dir, OP_READ)) {
        for (size_t i = 0; i < ARRCNT(dv500_i2c5); i++)
            dv500_i2c5[i].owed = false; /* we no longer know what is there */
        return;
    }

    /* An undo outlives a re-arm only while the pad still holds our word. The
     * clock above cannot do this -- its word is the SDK's word too -- but
     * ours keeps the found drive/pull bits and changes only the function,
     * while the vendor's I2C5 setup writes its own (0x1752), so "still ours"
     * is readable here. Without it the arm that follows the SPI probe, which
     * has no cleanup of its own, would find I2C5 already set, take nothing,
     * and leave the pads switched after we exit. */
    for (size_t i = 0; i < 2; i++)
        if (dv500_i2c5[i].owed && cur[i] != dv500_i2c5[i].wrote)
            dv500_i2c5[i].owed = false;

    for (size_t i = 0; i < 2; i++) {
        const uint32_t func = cur[i] & DV500_IOCFG_FUNC_MASK;
        if (func == DV500_IOCFG_FUNC_I2C5)
            continue;
        if (func != DV500_IOCFG_FUNC_GPIO || (dir & (1u << i)))
            return; /* somebody's SPI1 or GPIO output: not ours, either pad */
    }

    for (size_t i = 0; i < 2; i++) {
        if ((cur[i] & DV500_IOCFG_FUNC_MASK) == DV500_IOCFG_FUNC_I2C5)
            continue;
        sensor_crg_t *st = &dv500_i2c5[i];
        st->found = cur[i];
        st->wrote = (cur[i] & ~DV500_IOCFG_FUNC_MASK) | DV500_IOCFG_FUNC_I2C5;
        if (mem_reg(dv500_i2c5_pad[i], &st->wrote, OP_WRITE))
            st->owed = true;
    }
}

static void ot_ensure_sensor_enabled(void) {
    for (size_t i = 0; i < ARRCNT(ot_crg_addr); i++)
        sensor_crg_enable(
            &ot_crg[i], ot_crg_addr[i], CV610_PERI_CRG_SENSOR0_CKEN,
            CV610_PERI_CRG_SENSOR0_SRST, CV610_PERI_CRG_SENSOR_CKSEL_MASK,
            CV610_PERI_CRG_SENSOR_CKSEL_24M);
}

/* The vendor SDK's sensor buses on DV500: SENSOR0..3 = I2C4, I2C5, I2C3,
 * I2C6 (sample_comm_vi.c, sample_vio.c, sample_dis_two_pipe.c). */
static const int dv500_sensor_buses[] = {4, 5, 3, 6, -1};

static void ot_ensure_sensor_restored() {
    for (size_t i = 0; i < ARRCNT(ot_crg_addr); i++)
        sensor_crg_restore(&ot_crg[i], ot_crg_addr[i]);
    for (size_t i = 0; i < ARRCNT(dv500_i2c5); i++)
        sensor_crg_restore(&dv500_i2c5[i], dv500_i2c5_pad[i]);
}

/* Installed as hal_enable_sensor_clock so the bus sweep in getsensorid() can
 * re-arm before each attempt.
 *
 * It used to need to. hisi_hal_cleanup() gated the clock off again the moment
 * a probe finished, so everything after the first bus in the sweep would have
 * read an unclocked sensor. Under the rules in sensor_crg_enable() the
 * cleanup now only undoes an ungate it actually performed, so the re-arm is
 * belt and braces on V4 and OT -- and still load-bearing on Ingenic, whose
 * vendor SDK gates the clock from outside this process entirely. */
#endif /* IPCHW_HISI_V5 */

/* A switch, like hisi_get_temp() and hisi_get_die_id() below: it takes a
 * per-generation guard on each arm without the dangling else an #ifdef'd
 * if/else chain leaves behind, and a build that kept every generation emits
 * what was here before. */
static void hisi_ensure_sensor_enabled() {
    switch (chip_generation) {
#ifdef IPCHW_HISI_V3
    case HISI_V3:
        v3_ensure_sensor_enabled();
        break;
#endif
#ifdef IPCHW_HISI_V4
    case HISI_V4:
        v4_ensure_sensor_enabled();
        break;
#endif
#ifdef IPCHW_HISI_V5
    case HISI_OT:
        ot_ensure_sensor_enabled();
        break;
#endif
#ifdef IPCHW_HISI_GOKE7206
    case GOKE_7206:
        gk7206_ensure_sensor_enabled();
        break;
#endif
    default:
        break;
    }
}

/* What a probe arms: the clocks, as setup_hal_hisi() also does, plus on DV500
 * the I2C5 pads. Those are switched only here, never at setup, because only a
 * probe is followed by the hal_cleanup() that switches them back -- a
 * chip-name or clocks query goes through setup and nothing else. */
static void hisi_ensure_sensor_ready() {
    hisi_ensure_sensor_enabled();
#ifdef IPCHW_HISI_V5
    if (chip_generation == HISI_OT && is_dv500())
        dv500_i2c5_enable();
#endif
}

static void hisi_hal_cleanup() {
    switch (chip_generation) {
#ifdef IPCHW_HISI_V4
    case HISI_V4:
        v4_ensure_sensor_restored();
        break;
#endif
#ifdef IPCHW_HISI_V5
    case HISI_OT:
        ot_ensure_sensor_restored();
        break;
#endif
#ifdef IPCHW_HISI_GOKE7206
    case GOKE_7206:
        gk7206_ensure_sensor_restored();
        break;
#endif
    default:
        break;
    }
    restore_printk();
}

#ifndef STANDALONE_LIBRARY
static void get_hisi_sdk(cJSON *j_inner) {
    char buf[1024];

    if (line_from_file("/proc/umap/sys", "Version: \\[(.+)\\]", buf,
                       sizeof(buf))) {
        char *ptr = strchr(buf, ']');
        char *build = strchr(buf, '[');
        if (!ptr || !build)
            return;
        *ptr++ = ' ';
        *ptr++ = '(';
        /* build+1 and ptr alias the same buffer (the bracketed build
         * time sits after the ']' we just overwrote), so this is an
         * overlapping copy: strcpy() is UB here (ASAN: strcpy-param-
         * overlap), memmove() is well-defined and yields the same
         * "<version> (<build time>)" string. */
        memmove(ptr, build + 1, strlen(build + 1) + 1);
        strcat(ptr, ")");
        ADD_PARAM("sdk", buf);
    }
}
#endif

void setup_hal_hisi() {
    hal_enable_sensor_clock = hisi_ensure_sensor_ready;
    hisi_ensure_sensor_enabled();
#ifdef IPCHW_HISI_V5
    if (chip_generation == HISI_OT && is_dv500())
        hal_sensor_buses = dv500_sensor_buses;
#endif

    open_i2c_sensor_fd = hisi_open_i2c_fd;
    open_spi_sensor_fd = hisi_open_spi_fd;
    hal_cleanup = hisi_hal_cleanup;
    /* V2 and V2A share a back-end, so they share a guard: a V2-only build
     * still carries the HISI_V2A label, which is inert because the chip-ID
     * table that would return that generation is not compiled in. */
    switch (chip_generation) {
#ifdef IPCHW_HISI_V1
    case HISI_V1:
        open_i2c_sensor_fd = hisi_gen1_open_i2c_sensor_fd;
        open_spi_sensor_fd = hisi_gen1_open_spi_sensor_fd;
        i2c_read_register = hisi_gen1_sensor_read_register;
        i2c_write_register = hisi_gen1_sensor_write_register;
        spi_read_register = sony_ssp_read_register;
        break;
#endif
#if defined(IPCHW_HISI_V2) || defined(IPCHW_HISI_V2A)
    case HISI_V2:
    case HISI_V2A:
        i2c_read_register = hisi_gen2_sensor_read_register;
        i2c_write_register = hisi_gen2_sensor_write_register;
        i2c_change_addr = i2c_change_plain_addr;
        break;
#endif
    default:
        i2c_read_register = hisi_sensor_read_register;
        i2c_write_register = hisi_sensor_write_register;
        break;
    }
    possible_i2c_addrs = my_possible_i2c_addrs;
#ifdef IPCHW_HISI_3536D
    /* The Hi3536DV100 is a decoder with no video input, so there is no sensor
     * to find, and probing for one is not free. Its i2c0 has nothing on the
     * wire on an NVR, and the kernel's hibvt-i2c runs it in polling mode (the
     * device tree gives it no interrupt): every transfer spins udelay(50)
     * 0x10000 times, 3.3 s on the SoC's only core, before "wait idle timeout".
     * The full table is 38 such transfers -- the board stopped answering ping
     * for two minutes on every report. */
    if (chip_generation == HISI_3536D)
        possible_i2c_addrs = NULL;
#endif
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
    uint32_t readed;

    if (mem_reg(prep_addr, &readed, OP_READ)) {
        if (readed != prep_val) {
            mem_reg(prep_addr, &prep_val, OP_WRITE);
            usleep(100000);
        }
    }

    if (mem_reg(read_addr, &readed, OP_READ)) {
        return readed & ((1 << temp_bitness) - 1);
    }
    return 0;
}

// PMC register 68
#define CV200_PERI_PMC68 0x20270110
// PMC register 69
#define CV200_PERI_PMC69 0x20270114

// Tsensor control register
#define AV200_PERI_PMC68 0x120A0110
// Tsensor measurement value 0/1 register
#define AV200_PERI_PMC70 0x120A0118

// T-sensor temperature record register 0
#define CV300_MISC_CTRL41 0x120300A4
// Temperature sensor (T-Sensor) control register
#define CV300_MISC_CTRL39 0x1203009C

// T-Sensor temperature record register 0
#define AV300_MISC_CTRL47 0x120300BC
// Temperature sensor (T-Sensor) control register
#define AV300_MISC_CTRL45 0x120300B4

// T-Sensor temperature record register 0
#define EV300_MISC_CTRL47 0x120280BC
// Temperature sensor (T-Sensor) control register
#define EV300_MISC_CTRL45 0x120280B4

#define HI3536_PERI_PMC68 0x120e0110
#define HI3536_PERI_PMC70 0x120e0118

#define CV610_TSENSOR_CTRL2 0x1102A008
#define CV610_PERI_CRG4560 0x11014740

#ifdef IPCHW_HISI_GOKE7206
/* GK7206V1x (the SDK's xm7206v1x, machine "xmorca"). The T-sensor block sits
 * one word above EV300's, so EV300's addresses land on the wrong registers:
 * its "MISC_CTRL47" is the config word here, and its "MISC_CTRL45" is not
 * part of the block at all. Layout and formula are the vendor kernel's
 * lotus/misc/tsensor.c and machine/xmorca/include/platform.h. */
#define GK7206_TSENSOR_CTRL 0x120280B8
#define GK7206_TSENSOR_RECORD 0x120280C0
#define GK7206_TSENSOR_EN (1u << 31)
/* What the vendor driver leaves in CTRL: enabled, one-shot circulation,
 * averaging bypassed, one 2.048 ms period. */
#define GK7206_TSENSOR_CFG 0xE0100000
/* Per-die calibration in the OTP shadow: bits[11:0] T0 in 1/100 degC,
 * bits[22:12] Y0, the code read at T0. Either one zero means unfused, and
 * the driver falls back to these. */
#define GK7206_OTP_TSENSOR 0x12020414
#define GK7206_DEFAULT_Y0 512
#define GK7206_DEFAULT_T0 4764

static float gk7206_get_temp() {
    uint32_t val;

    if (!mem_reg(GK7206_TSENSOR_CTRL, &val, OP_READ))
        return NAN;
    if (!(val & GK7206_TSENSOR_EN)) {
        val = GK7206_TSENSOR_CFG;
        if (!mem_reg(GK7206_TSENSOR_CTRL, &val, OP_WRITE))
            return NAN;
    }

    int y0 = GK7206_DEFAULT_Y0, t0 = GK7206_DEFAULT_T0;
    if (mem_reg(GK7206_OTP_TSENSOR, &val, OP_READ)) {
        const int y = (val >> 12) & 0x7ff, t = val & 0xfff;
        if (y && t) {
            y0 = y;
            t0 = t;
        }
    }

    /* The record reads 0 until the first conversion after enabling. */
    uint32_t code = 0;
    for (int i = 0; i < 50 && !code; i++) {
        if (!mem_reg(GK7206_TSENSOR_RECORD, &val, OP_READ))
            return NAN;
        code = val & 0x3ff;
        if (!code)
            usleep(2000);
    }
    if (!code)
        return NAN;

    /* T = (Y - Y0) * 0.303 + T0, with the driver's 1241/4096 for 0.303 */
    return ((int)code - y0) * 1241 / 4096.0f + t0 / 100.0f;
}
#endif

static float hisi_get_temp() {
    float tempo;
    switch (chip_generation) {
#ifdef IPCHW_HISI_V2
    case HISI_V2:
        // PERI_PMC69 bit[7:0]
        tempo =
            hisi_reg_temp(CV200_PERI_PMC69, 8, CV200_PERI_PMC68, 0x60FA0000);
        tempo = ((tempo * 180) / 256) - 40;
        break;
#endif
#ifdef IPCHW_HISI_V3A
    case HISI_V3A:
        // PERI_PMC70 bit[9:0]
        tempo =
            hisi_reg_temp(AV200_PERI_PMC70, 10, AV200_PERI_PMC68, 0x60FA0000);
        tempo = ((tempo - 125) / 806) * 165 - 40;
        break;
#endif
#ifdef IPCHW_HISI_V3
    case HISI_V3:
        // MISC_CTRL41 bit[9:0]
        tempo =
            hisi_reg_temp(CV300_MISC_CTRL41, 10, CV300_MISC_CTRL39, 0x60FA0000);
        tempo = ((tempo - 125) / 806) * 165 - 40;
        break;
#endif
#ifdef IPCHW_HISI_V4A
    case HISI_V4A:
        // MISC_CTRL47 bit[9:0]
        tempo =
            hisi_reg_temp(AV300_MISC_CTRL47, 10, AV300_MISC_CTRL45, 0x60FA0000);
        tempo = ((tempo - 136) / 793 * 165) - 40;
        break;
#endif
#ifdef IPCHW_HISI_V4
    case HISI_V4:
        // MISC_CTRL47 bit[9:0]
        tempo =
            hisi_reg_temp(EV300_MISC_CTRL47, 10, EV300_MISC_CTRL45, 0xC3200000);
        tempo = ((tempo - 117) / 798) * 165 - 40;
        break;
#endif
#ifdef IPCHW_HISI_3536D
    case HISI_3536D:
        // PMC70 bit[9:0]
        tempo =
            hisi_reg_temp(HI3536_PERI_PMC70, 10, HI3536_PERI_PMC68, 0x40000000);
        tempo = ((tempo * 180) / 256) - 40;
        break;
#endif
#ifdef IPCHW_HISI_3536C
    case HISI_3536C:
        // PMC70 bit[9:0]
        tempo =
            hisi_reg_temp(HI3536_PERI_PMC70, 10, HI3536_PERI_PMC68, 0x40000000);
        tempo = ((tempo - 125) / 806) * 165 - 40;
        break;
#endif
#ifdef IPCHW_HISI_V5
    case HISI_OT:
        // TSENSOR_CTRL2 bit[9:0]
        tempo =
            hisi_reg_temp(CV610_TSENSOR_CTRL2, 10, CV610_PERI_CRG4560, 0x11);
        tempo = ((tempo - 127) / 784 * 165) - 40;
        break;
#endif
#ifdef IPCHW_HISI_GOKE7206
    case GOKE_7206:
        return gk7206_get_temp();
#endif
    default:
        return NAN;
    }

    return tempo;
}

#ifdef IPCHW_HISI_V1
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
#endif /* IPCHW_HISI_V1 */

#ifdef IPCHW_HISI_V2A
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
#endif /* IPCHW_HISI_V2A */

#ifdef IPCHW_HISI_V2
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
#endif /* IPCHW_HISI_V2 */

#ifdef IPCHW_HISI_V3A
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
#endif /* IPCHW_HISI_V3A */

#ifdef IPCHW_HISI_V3
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
#endif /* IPCHW_HISI_V3 */

#ifdef IPCHW_HISI_GOKE7206
/* The GK7206 family (SDK names: V10, V10B, V11, V11A, V11AT, V12A) is told
 * apart by the model code in the low byte of eFuse word 0x10090030, whose high
 * half is "GK" (0x474B); V11 and V11A, at least, share chip ID 0x72060110.
 * The vendor modules accept 0x474B00B7..BD, C7..CD and D7..DD (xm_common),
 * and xm_vi/xm_venc sort them into capability classes: {B7, BC, C7, CC} with
 * 3840x2160 and a 3840 line, {CA, D8..DB, DD} with 3840x2160 and a 4032 line,
 * and every other code with 3840x3840.
 *
 * Nothing in the SDK or in XM's firmware names the codes. Only 0xCA is
 * confirmed (an IVG-K8, XM IPC_GK7206V11A). THE REST IS GUESSED from that
 * pattern: the high nibble taken as the DDR tier the SDK's load script gives
 * each part (B 64 MB: V10, V10B; C 128 MB: V11, V11A, V11AT; D 256 MB: V12A),
 * the low nibble 7 as the base part, C as the tier's other part, A as the A
 * part. The 3840x3840 class matches no SDK part and is left to the chip ID.
 *
 * The eFuse reads 0x2 until its clock (CRG 0x1BC bit 1) is on; it is turned
 * back off if it was off. */
#define GK7206_CRG_OTP 0x120101BC
#define GK7206_CRG_OTP_CKEN (1u << 1)
#define GK7206_EFUSE_MODEL 0x10090030

static const char *gk7206_variant(const char *by_chip_id) {
    uint32_t crg, on, cur, word = 0;

    if (!mem_reg(GK7206_CRG_OTP, &crg, OP_READ))
        return by_chip_id;
    on = crg | GK7206_CRG_OTP_CKEN;
    if (on != crg && !mem_reg(GK7206_CRG_OTP, &on, OP_WRITE))
        return by_chip_id;
    mem_reg(GK7206_EFUSE_MODEL, &word, OP_READ);
    /* Undo only our own ungate: if the register changed meanwhile, whoever
     * changed it owns it now (as sensor_crg_restore() does). */
    if (on != crg && mem_reg(GK7206_CRG_OTP, &cur, OP_READ) && cur == on)
        mem_reg(GK7206_CRG_OTP, &crg, OP_WRITE);

    if (word >> 16 != 0x474B)
        return by_chip_id;
    switch (word & 0xff) {
    case 0xB7: /* guessed */
        return "7206V10";
    case 0xBC: /* guessed */
        return "7206V10B";
    case 0xC7: /* guessed */
        return "7206V11";
    case 0xCA: /* confirmed: IVG-K8 (XM IPC_GK7206V11A), 0x474B00CA */
        return "7206V11A";
    case 0xCC: /* guessed */
        return "7206V11AT";
    case 0xD8: /* guessed, all five */
    case 0xD9:
    case 0xDA:
    case 0xDB:
    case 0xDD:
        return "7206V12A";
    default:
        return by_chip_id;
    }
}
#endif

static const char *get_hisi_chip_id(uint32_t family_id, uint8_t scsysid0) {
    switch (family_id) {
#ifdef IPCHW_HISI_V2A
    case 0x3516A100:
        chip_generation = HISI_V2A;
        return get_chip_V2A(scsysid0);
#endif
#ifdef IPCHW_HISI_V3A
    case 0x35190101:
        chip_generation = HISI_V3A;
        return get_chip_V3A(scsysid0);
#endif
#ifdef IPCHW_HISI_V4A
    case 0x3516A300:
        chip_generation = HISI_V4A;
        return "3516AV300";
#endif
#ifdef IPCHW_HISI_V3
    case 0x3516C300:
        chip_generation = HISI_V3;
        return get_chip_V3(scsysid0);
#endif
#ifdef IPCHW_HISI_V4A
    case 0x3516C500:
        chip_generation = HISI_V4A;
        return "3516CV500";
#endif
#ifdef IPCHW_HISI_V5
    case 0x3516C608:
        chip_generation = HISI_OT;
        return "3516CV608";
#endif
#ifdef IPCHW_HISI_V5
    case 0x3516C610:
        chip_generation = HISI_OT;
        return "3516CV610";
#endif
#ifdef IPCHW_HISI_V5
    case 0x3516C613:
        chip_generation = HISI_OT;
        return "3516CV613";
#endif
#ifdef IPCHW_HISI_V4
    case 0x3516D200:
        chip_generation = HISI_V4;
        return "3516DV200";
#endif
#ifdef IPCHW_HISI_V4A
    case 0x3516D300:
        chip_generation = HISI_V4A;
        return "3516DV300";
#endif
#ifdef IPCHW_HISI_V4
    case 0x3516E200:
        chip_generation = HISI_V4;
        return "3516EV200";
#endif
#ifdef IPCHW_HISI_V5
    case 0x3516D500:
        chip_generation = HISI_OT;
        return "3516DV500";
#endif
#ifdef IPCHW_HISI_V4
    case 0x3516E300:
        chip_generation = HISI_V4;
        return "3516EV300";
#endif
#ifdef IPCHW_HISI_V1
    case 0x35180100:
        chip_generation = HISI_V1;
        return get_chip_V1();
#endif
#ifdef IPCHW_HISI_V2
    case 0x3518E200:
        chip_generation = HISI_V2;
        return get_chip_V2(scsysid0);
#endif
#ifdef IPCHW_HISI_V4
    case 0x3518E300:
        chip_generation = HISI_V4;
        return "3518EV300";
#endif
#ifdef IPCHW_HISI_V5
    case 0x3519D500:
        chip_generation = HISI_OT;
        return "3519DV500";
#endif
#ifdef IPCHW_HISI_3536C
    case 0x3536C100:
        chip_generation = HISI_3536C;
        return "3536CV100";
#endif
#ifdef IPCHW_HISI_3536D
    case 0x3536D100:
        chip_generation = HISI_3536D;
        return "3536DV100";
#endif
    case 0x3520D100:
        return "3520DV200";
    case 0x35210100:
        return "3521V100";
    case 0x3559A100:
        return "3559AV100";
    case 0xBDA9D100:
        switch (scsysid0) {
            case 0:
            case 1:
                return "3521DV100";
#ifdef IPCHW_HISI_3536C
            case 2:
                chip_generation = HISI_3536C;
                return "3536CV100";
#endif
            case 3:
                return "3520DV400";
            default:
                fprintf(stderr, "reserved value %#x", scsysid0);
                return "unknown";
        }
#ifdef IPCHW_HISI_V4
    case 0x72050200:
        // former 3516EV200
        chip_generation = HISI_V4;
        return "7205V200";
#endif
#ifdef IPCHW_HISI_V4
    case 0x72020300:
        // former 3518EV300
        chip_generation = HISI_V4;
        return "7202V300";
#endif
#ifdef IPCHW_HISI_V4
    case 0x72050300:
        // former 3516EV300
        chip_generation = HISI_V4;
        return "7205V300";
#endif
#ifdef IPCHW_HISI_V4
    case 0x76050100:
        // former 3516DV200
        chip_generation = HISI_V4;
        return "7605V100";
#endif
#ifdef IPCHW_HISI_V4
    case 0x72050210:
        // new chip in the line?
        chip_generation = HISI_V4;
        return "7205V210";

#endif
#ifdef IPCHW_HISI_V4
    case 0x72010200:
        chip_generation = HISI_V4;
        return "7201V200";
#endif
#ifdef IPCHW_HISI_V4
    case 0x72010300:
        chip_generation = HISI_V4;
        return "7201V300";
#endif
#ifdef IPCHW_HISI_V4
    case 0x72020330:
        chip_generation = HISI_V4;
        return "7202V330";

#endif
#ifdef IPCHW_HISI_V4
    case 0x72050500:
        // A new chip that was received in the OpenIPC lab 2023.07.28
        chip_generation = HISI_V4;
        return "7205V500";
#endif
#ifdef IPCHW_HISI_V4
    case 0x72050510:
        chip_generation = HISI_V4;
        return "7205V510";
#endif
#ifdef IPCHW_HISI_V4
    case 0x72050530:
        chip_generation = HISI_V4;
        return "7205V530";
#endif
#ifdef IPCHW_HISI_GOKE7206
    case 0x72060100:
        // expected by the SDK's numbering (XM7605V11 is 0x76050110), unseen
        chip_generation = GOKE_7206;
        return gk7206_variant("7206V10");
    case 0x72060110:
        chip_generation = GOKE_7206;
        return gk7206_variant("7206V11");
    case 0x72060120:
        // expected by the SDK's numbering, unseen
        chip_generation = GOKE_7206;
        return gk7206_variant("7206V12");
#endif

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
