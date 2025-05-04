#ifndef HAL_ROCKCHIP_H
#define HAL_ROCKCHIP_H

#include <stdbool.h>

#define RV1106 0x1106
#define RV1109 0x1109
#define RV1126 0x1126

bool rockchip_detect_cpu(char *chip_name);
unsigned long rockchip_totalmem(unsigned long *media_mem);
void rockchip_setup_hal();

#endif /* HAL_ROCKCHIP_H */
