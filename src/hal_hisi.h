#ifndef HAL_HISI_H
#define HAL_HISI_H

#include <math.h>
#include <stdbool.h>
#include <sys/types.h>

#include <cjson/cJSON.h>

#include "sensors.h"

#define VENDOR_HISI "HiSilicon"
#define VENDOR_GOKE "Goke"

#define HISI_V1 0x35180100
#define HISI_V2 0x3518E200
#define HISI_V2A 0x3516A100
#define HISI_V3 0x3516C300
#define HISI_V4 0x3516E300
#define HISI_V4A 0x3516C500

// V4
#define HI_MIPI_RESET_MIPI 0X40046D07
#define HI_MIPI_RESET_SENSOR 0X40046D05
#define HI_MIPI_SET_DEV_ATTR 0X40886D01
#define HI_MIPI_UNRESET_MIPI 0X40046D08
#define HI_MIPI_UNRESET_SENSOR 0X40046D06

int hisi_SYS_DRV_GetChipId();
const char *hisi_cv100_get_mii_mux();
bool hisi_ev300_get_die_id(char *buf, ssize_t len);

int sony_ssp_read_register(int fd, unsigned char i2c_addr,
                           unsigned int reg_addr, unsigned int reg_width,
                           unsigned int data_width);

int hisi_gen3_spi_read_register(int fd, unsigned char i2c_addr,
                                unsigned int reg_addr, unsigned int reg_width,
                                unsigned int data_width);

void hisi_vi_information(sensor_ctx_t *ctx);
unsigned long hisi_totalmem(unsigned long *media_mem);
void hisi_detect_fmc();
bool hisi_detect_cpu(uint32_t SC_CTRL_base);
size_t hisi_sizeof_combo_dev_attr();
void hisi_dump_combo_dev_attr(void *ptr);

#endif /* HAL_HISI_H */
