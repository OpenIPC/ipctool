#ifndef HAL_HISI_H
#define HAL_HISI_H

#include <math.h>
#include <stdbool.h>
#include <sys/types.h>

#include <cjson/cJSON.h>

#include "sensors.h"

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

#define IS_16EV200 IS_CHIP("3516EV200") || IS_CHIP("7205V200") || IS_CHIP("7205V210") || IS_CHIP("7201V200") || IS_CHIP("7201V300")
#define IS_16EV300 IS_CHIP("3516EV300") || IS_CHIP("7205V300")
#define IS_18EV300 IS_CHIP("3518EV300") || IS_CHIP("7202V300") || IS_CHIP("7202V330")
#define IS_16DV200 IS_CHIP("3516DV200") || IS_CHIP("7605V100")
#define IS_7205V500 IS_CHIP("7205V500") || IS_CHIP("7205V510") || IS_CHIP("7205V530")


bool hisi_ev300_get_die_id(char *buf, ssize_t len);
void hisi_vi_information(sensor_ctx_t *ctx);
unsigned long hisi_totalmem(unsigned long *media_mem);
bool hisi_detect_cpu(char *chip_name, uint32_t SC_CTRL_base);
void setup_hal_hisi();

#endif /* HAL_HISI_H */
