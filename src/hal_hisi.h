#ifndef HAL_HISI_H
#define HAL_HISI_H

#include <stdbool.h>
#include <sys/types.h>

#define VENDOR_HISI "HiSilicon"

int hisi_SYS_DRV_GetChipId();
const char *hisi_cv100_get_sensor_clock();
const char *hisi_cv100_get_sensor_data_type();
const char *hisi_cv200_get_sensor_clock();
const char *hisi_cv200_get_sensor_data_type();
const char *hisi_cv300_get_sensor_clock();
const char *hisi_cv300_get_sensor_data_type();
const char *hisi_ev300_get_sensor_clock();
const char *hisi_ev300_get_sensor_data_type();
const char *hisi_cv100_get_mii_mux();
bool hisi_ev300_get_die_id(char *buf, ssize_t len);
int hisi_get_temp();

int sony_ssp_read_register(int fd, unsigned char i2c_addr,
                           unsigned int reg_addr, unsigned int reg_width,
                           unsigned int data_width);

int hisi_gen3_spi_read_register(int fd, unsigned char i2c_addr,
                                unsigned int reg_addr, unsigned int reg_width,
                                unsigned int data_width);

#endif /* HAL_HISI_H */
