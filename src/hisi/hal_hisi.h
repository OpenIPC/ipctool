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
#define HISI_V3A 0x35190101
#define HISI_V3 0x3516C300
#define HISI_V4 0x3516E300
#define HISI_V4A 0x3516C500
#define HISI_3536C 0x3536C100
#define HISI_3536D 0x3536D100

#define IS_CHIP(name) (!strcmp(chip_name, name))

#define IS_16EV200 IS_CHIP("3516EV200") || IS_CHIP("7205V200")
#define IS_16EV300 IS_CHIP("3516EV300") || IS_CHIP("7205V300")
#define IS_18EV300 IS_CHIP("3518EV300") || IS_CHIP("7202V300")
#define IS_16DV200 IS_CHIP("3516DV200") || IS_CHIP("7605V100")

bool hisi_ev300_get_die_id(char *buf, ssize_t len);
void hisi_vi_information(sensor_ctx_t *ctx);
unsigned long hisi_totalmem(unsigned long *media_mem);
void hisi_detect_fmc();
bool hisi_detect_cpu(uint32_t SC_CTRL_base);

#endif /* HAL_HISI_H */
