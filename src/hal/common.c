#include <stdbool.h>
#include <string.h>

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "chipid.h"
#include "hal/common.h"

int i2c_adapter_nr = 0;
sensor_addr_t *possible_i2c_addrs;
int (*open_i2c_sensor_fd)();
int (*open_spi_sensor_fd)();
bool (*close_sensor_fd)(int fd);
read_register_t i2c_read_register;
read_register_t spi_read_register;
write_register_t i2c_write_register;
write_register_t spi_write_register;
int (*i2c_change_addr)(int fd, unsigned char addr);
float (*hal_temperature)();
void (*hal_cleanup)();

#ifndef STANDALONE_LIBRARY
void (*hal_detect_ethernet)(cJSON *root);
unsigned long (*hal_totalmem)(unsigned long *media_mem);
const char *(*hal_fmc_mode)(void);
void (*hal_chip_properties)(cJSON *root);
void (*hal_firmware_props)(cJSON *root);
#endif

int universal_open_sensor_fd(const char *dev_name) {
    int fd;

    fd = open(dev_name, O_RDWR);
    if (fd < 0) {
#ifndef NDEBUG
        printf("Open %s error!\n", dev_name);
#endif
        return -1;
    }

    return fd;
}

bool universal_close_sensor_fd(int fd) {
    if (fd < 0)
        return false;

    return close(fd) == 0;
}

// Set I2C slave address,
// actually do nothing
int dummy_sensor_i2c_change_addr(int fd, unsigned char addr) {
    (void)fd;
    (void)addr;

    return 0;
}

// Universal I2C code
int i2c_changenshift_addr(int fd, unsigned char addr) {
    if (ioctl(fd, I2C_SLAVE_FORCE, addr >> 1) < 0) {
        return -1;
    }
    return 0;
}

int i2c_change_plain_addr(int fd, unsigned char addr) {
    if (ioctl(fd, I2C_SLAVE_FORCE, addr) < 0) {
        return -1;
    }
    return 0;
}

int universal_i2c_write_register(int fd, unsigned char i2c_addr,
                                 unsigned int reg_addr, unsigned int reg_width,
                                 unsigned int data, unsigned int data_width) {
    (void)i2c_addr;
    (void)data;
    char buf[2];

    if (reg_width == 2) {
        buf[0] = (reg_addr >> 8) & 0xff;
        buf[1] = reg_addr & 0xff;
    } else {
        buf[0] = reg_addr & 0xff;
    }

    if (write(fd, buf, data_width) != (int)data_width) {
        return -1;
    }
    return 0;
}

int universal_i2c_read_register(int fd, unsigned char i2c_addr,
                                unsigned int reg_addr, unsigned int reg_width,
                                unsigned int data_width) {
    (void)i2c_addr;
    unsigned char recvbuf[4];
    unsigned int data;

    if (reg_width == 2) {
        recvbuf[0] = (reg_addr >> 8) & 0xff;
        recvbuf[1] = reg_addr & 0xff;
    } else {
        recvbuf[0] = reg_addr & 0xff;
    }

    int data_size = reg_width * sizeof(unsigned char);
    if (write(fd, recvbuf, data_size) != data_size) {
        return -1;
    }

    data_size = data_width * sizeof(unsigned char);
    if (read(fd, recvbuf, data_size) != data_size) {
        return -1;
    }

    if (data_width == 2) {
        data = recvbuf[0] | (recvbuf[1] << 8);
    } else
        data = recvbuf[0];

    return data;
}

unsigned int sony_i2c_to_spi(unsigned int reg_addr) {
    if (reg_addr >= 0x3000)
        return reg_addr - 0x3000 + 0x200;
    else
        return reg_addr;
}

static int universal_spi_read_register(int fd, unsigned char i2c_addr,
                                       unsigned int reg_addr,
                                       unsigned int reg_width,
                                       unsigned int data_width) {
    (void)i2c_addr;
    (void)reg_width;
    (void)data_width;
    int ret = 0;
    struct spi_ioc_transfer mesg[1];
    unsigned char tx_buf[8] = {0};
    unsigned char rx_buf[8] = {0};

    reg_addr = sony_i2c_to_spi(reg_addr);

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

int universal_spi_write_register(int fd, unsigned char i2c_addr,
                                 unsigned int reg_addr, unsigned int reg_width,
                                 unsigned int data, unsigned int data_width) {
    (void)i2c_addr;
    (void)reg_width;
    (void)data_width;
    int ret = 0;
    struct spi_ioc_transfer mesg[1];
    unsigned char tx_buf[8] = {0};
    unsigned char rx_buf[8] = {0};

    reg_addr = sony_i2c_to_spi(reg_addr);

    tx_buf[0] = (reg_addr & 0xff00) >> 8;
    tx_buf[1] = reg_addr & 0xff;
    tx_buf[2] = data;
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

    return 0;
}

static int fallback_open_sensor_fd(int i2c_adapter_nr) {
    char adapter_name[FILENAME_MAX];

    snprintf(adapter_name, sizeof(adapter_name), "/dev/i2c-%d", i2c_adapter_nr);
    return universal_open_sensor_fd(adapter_name);
}

static void universal_hal_cleanup() {}

static unsigned long default_totalmem(unsigned long *media_mem) {
    (void)media_mem;

    return kernel_mem();
}

void setup_hal_fallback() {
    open_i2c_sensor_fd = fallback_open_sensor_fd;
    close_sensor_fd = universal_close_sensor_fd;
    i2c_change_addr = i2c_changenshift_addr;
    i2c_read_register = universal_i2c_read_register;
    spi_read_register = universal_spi_read_register;
    i2c_write_register = universal_i2c_write_register;
    spi_write_register = universal_spi_write_register;
    hal_cleanup = universal_hal_cleanup;
#ifndef STANDALONE_LIBRARY
    hal_totalmem = default_totalmem;
#endif
}

typedef struct meminfo {
    unsigned long MemTotal;
} meminfo_t;
meminfo_t mem;

static void parse_meminfo(struct meminfo *g) {
    char buf[60];
    FILE *fp;
    int seen_cached_and_available_and_reclaimable;

    fp = fopen("/proc/meminfo", "r");
    g->MemTotal = 0;
    while (fgets(buf, sizeof(buf), fp)) {
        if (sscanf(buf, "MemTotal: %lu %*s\n", &g->MemTotal) == 1)
            break;
    }
    fclose(fp);
}

unsigned long kernel_mem() {
    if (!mem.MemTotal)
        parse_meminfo(&mem);
    return mem.MemTotal;
}

uint32_t rounded_num(uint32_t n) {
    int i;
    for (i = 0; n; i++) {
        n /= 2;
    }
    return 1 << i;
}
