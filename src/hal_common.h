#ifndef HAL_COMMON_H
#define HAL_COMMON_H

#include "hal_hisi.h"
#include "hal_xm.h"

#define SENSOR_ONSEMI 1
#define SENSOR_SOI 2
#define SENSOR_SONY 3
#define SENSOR_SMARTSENS 4

typedef struct {
    int sensor_type;
    unsigned char* addrs;
} sensor_addr_t;

extern sensor_addr_t* possible_i2c_addrs;

extern int (*open_sensor_fd)();
extern int (*sensor_i2c_change_addr)(int fd, unsigned char addr);
extern int (*sensor_read_register)(int fd, unsigned char i2c_addr,
                                   unsigned int reg_addr,
                                   unsigned int reg_width,
                                   unsigned int data_width);
extern int (*sensor_write_register)(int fd, unsigned char i2c_addr,
                                    unsigned int reg_addr,
                                    unsigned int reg_width, unsigned int data,
                                    unsigned int data_width);

void setup_hal_drivers();
void setup_hal_hisi();
void setup_hal_xm();
int common_open_sensor_fd(const char *dev_name);
int common_sensor_i2c_change_addr(int fd, unsigned char addr);

#endif /* HAL_COMMON_H */
