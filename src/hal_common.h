#ifndef HAL_COMMON_H
#define HAL_COMMON_H

#include "hal_hisi.h"
#include "hal_sstar.h"
#include "hal_xm.h"

enum SENSORS {
    SENSOR_ONSEMI = 1,
    SENSOR_SOI,
    SENSOR_SONY,
    SENSOR_SMARTSENS,
    SENSOR_OMNIVISION,
    SENSOR_BRIGATES,
};

typedef struct {
    int sensor_type;
    unsigned char *addrs;
} sensor_addr_t;

extern sensor_addr_t *possible_i2c_addrs;

extern int (*open_sensor_fd)();
extern void (*close_sensor_fd)(int fd);
extern int (*sensor_i2c_change_addr)(int fd, unsigned char addr);
extern int (*sensor_read_register)(int fd, unsigned char i2c_addr,
                                   unsigned int reg_addr,
                                   unsigned int reg_width,
                                   unsigned int data_width);
extern int (*sensor_write_register)(int fd, unsigned char i2c_addr,
                                    unsigned int reg_addr,
                                    unsigned int reg_width, unsigned int data,
                                    unsigned int data_width);
extern float (*hal_temperature)();
extern void (*hal_cleanup)();

void setup_hal_drivers();
void setup_hal_hisi();
void setup_hal_xm();
int common_open_sensor_fd(const char *dev_name);
int common_sensor_i2c_change_addr(int fd, unsigned char addr);
unsigned long kernel_mem();
void hal_ram(unsigned long *media_mem, uint32_t *total_mem);
uint32_t rounded_num(uint32_t n);

#endif /* HAL_COMMON_H */
