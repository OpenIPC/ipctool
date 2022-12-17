#ifndef HAL_XM_H
#define HAL_XM_H

#include <stdint.h>

#define VENDOR_XM "Xiongmai"

#define CMD_I2C_WRITE 0x01
#define CMD_I2C_READ 0x03

typedef struct I2C_DATA_S {
    unsigned char dev_addr;
    unsigned int reg_addr;
    unsigned int addr_byte_num;
    unsigned int data;
    unsigned int data_byte_num;

} I2C_DATA_S;

int xm_sensor_read_register(int fd, unsigned char i2c_addr,
                            unsigned int reg_addr, unsigned int reg_width,
                            unsigned int data_width);

int xm_sensor_write_register(int fd, unsigned char i2c_addr,
                             unsigned int reg_addr, unsigned int reg_width,
                             unsigned int data, unsigned int data_width);
unsigned long xm_totalmem(unsigned long *media_mem);
bool xm_detect_cpu(char *chip_name, uint32_t base);
void setup_hal_xm();

#endif /* HAL_XM_H */
