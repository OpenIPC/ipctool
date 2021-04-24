#include <stdbool.h>
#include <string.h>

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "chipid.h"
#include "hal_common.h"

sensor_addr_t* possible_i2c_addrs;

int (*open_sensor_fd)();
void (*close_sensor_fd)(int fd);
int (*sensor_read_register)(int fd, unsigned char i2c_addr,
                            unsigned int reg_addr, unsigned int reg_width,
                            unsigned int data_width);
int (*sensor_write_register)(int fd, unsigned char i2c_addr,
                             unsigned int reg_addr, unsigned int reg_width,
                             unsigned int data, unsigned int data_width);
int (*sensor_i2c_change_addr)(int fd, unsigned char addr);
float (*hal_temperature)();
void (*hal_cleanup)();

int common_open_sensor_fd(const char *dev_name) {
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

bool common_close_sensor_fd(int fd) {
    if (fd >= 0) {
        close(fd);
        return true;
    }
    return false;
}

// Set I2C slave address,
// actually do nothing
int common_sensor_i2c_change_addr(int fd, unsigned char addr) { return 0; }

void setup_hal_drivers() {
    if (!strcmp(VENDOR_HISI, chip_manufacturer))
        setup_hal_hisi();
    else if (!strcmp(VENDOR_XM, chip_manufacturer))
        setup_hal_xm();
    else if (!strcmp(VENDOR_SSTAR, chip_manufacturer))
        setup_hal_sstar();
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

void hal_ram(unsigned long *media_mem, uint32_t *total_mem) {
    if (!strcmp(VENDOR_HISI, chip_manufacturer))
        *total_mem = hisi_totalmem(media_mem);
    else if (!strcmp(VENDOR_XM, chip_manufacturer))
        *total_mem = xm_totalmem(media_mem);
    else if (!strcmp(VENDOR_SSTAR, chip_manufacturer))
        *total_mem = sstar_totalmem(media_mem);

    if (!*total_mem)
        *total_mem = kernel_mem();
}
