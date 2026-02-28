#ifndef HAL_COMMON_H
#define HAL_COMMON_H

#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <linux/ioctl.h>
#include <sys/ioctl.h>

#include "cjson/cJSON.h"
#include "hal/allwinner.h"
#include "hal/bcm.h"
#include "hal/fh.h"
#include "hal/gm.h"
#include "hal/hisi/hal_hisi.h"
#include "hal/ingenic.h"
#include "hal/novatek.h"
#include "hal/rockchip.h"
#include "hal/sstar.h"
#include "hal/tegra.h"
#include "hal/xilinx.h"
#include "hal/xm.h"

#define SPI_CPHA 0x01
#define SPI_CPOL 0x02
#define SPI_MODE_3 (SPI_CPOL | SPI_CPHA)
#define SPI_CS_HIGH 0x04
#define SPI_LSB_FIRST 0x08

#define SPI_IOC_MAGIC 'k'
#define SPI_IOC_WR_MODE _IOW(SPI_IOC_MAGIC, 1, __u8)
#define SPI_IOC_WR_BITS_PER_WORD _IOW(SPI_IOC_MAGIC, 3, __u8)
#define SPI_IOC_WR_MAX_SPEED_HZ _IOW(SPI_IOC_MAGIC, 4, __u32)

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

#define SPI_MSGSIZE(N)                                                         \
    ((((N) * (sizeof(struct spi_ioc_transfer))) < (1 << _IOC_SIZEBITS))        \
         ? ((N) * (sizeof(struct spi_ioc_transfer)))                           \
         : 0)
#define SPI_IOC_MESSAGE(N) _IOW(SPI_IOC_MAGIC, 0, char[SPI_MSGSIZE(N)])

enum SENSORS {
    SENSOR_ONSEMI = 1,
    SENSOR_SOI,
    SENSOR_SONY,
    SENSOR_SMARTSENS,
    SENSOR_OMNIVISION,
    SENSOR_BRIGATES,
    SENSOR_GALAXYCORE,
    SENSOR_SUPERPIX,
    SENSOR_TECHPOINT,
    SENSOR_IMAGEDESIGN,
    SENSOR_VISEMI,
};

typedef struct {
    int sensor_type;
    unsigned char *addrs;
} sensor_addr_t;

extern int i2c_adapter_nr;
extern sensor_addr_t *possible_i2c_addrs;

typedef int (*read_register_t)(int fd, unsigned char i2c_addr,
                               unsigned int reg_addr, unsigned int reg_width,
                               unsigned int data_width);
typedef int (*write_register_t)(int fd, unsigned char i2c_addr,
                                unsigned int reg_addr, unsigned int reg_width,
                                unsigned int data, unsigned int data_width);

extern int (*open_i2c_sensor_fd)();
extern int (*open_spi_sensor_fd)();
extern bool (*close_sensor_fd)(int fd);
extern int (*i2c_change_addr)(int fd, unsigned char addr);
extern read_register_t i2c_read_register;
extern read_register_t spi_read_register;
extern write_register_t i2c_write_register;
extern write_register_t spi_write_register;
extern float (*hal_temperature)();
extern void (*hal_cleanup)();

#ifndef STANDALONE_LIBRARY
extern void (*hal_detect_ethernet)(cJSON *handle);
extern unsigned long (*hal_totalmem)(unsigned long *media_mem);
extern const char *(*hal_fmc_mode)(void);
extern void (*hal_chip_properties)(cJSON *root);
extern void (*hal_firmware_props)(cJSON *root);
#endif

void setup_hal_fallback();

int universal_open_sensor_fd(const char *dev_name);
bool universal_close_sensor_fd(int fd);

int dummy_sensor_i2c_change_addr(int fd, unsigned char addr);
int i2c_change_plain_addr(int fd, unsigned char addr);
int i2c_changenshift_addr(int fd, unsigned char addr);
int universal_i2c_write_register(int fd, unsigned char i2c_addr,
                                 unsigned int reg_addr, unsigned int reg_width,
                                 unsigned int data, unsigned int data_width);
int universal_i2c_read_register(int fd, unsigned char i2c_addr,
                                unsigned int reg_addr, unsigned int reg_width,
                                unsigned int data_width);
unsigned int sony_i2c_to_spi(unsigned int reg_addr);

unsigned long kernel_mem();
void hal_ram(unsigned long *media_mem, uint32_t *total_mem);
uint32_t rounded_num(uint32_t n);

#endif /* HAL_COMMON_H */
