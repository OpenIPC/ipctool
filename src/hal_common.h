#ifndef HAL_COMMON_H
#define HAL_COMMON_H

#include "hal_hisi.h"
#include "hal_xm.h"

extern int (*open_sensor_fd)();
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
int common_open_sensor_fd(const char* dev_name);

#endif /* HAL_COMMON_H */
