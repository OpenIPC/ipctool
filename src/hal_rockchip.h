#ifndef HAL_ROCKCHIP_H
#define HAL_ROCKCHIP_H

#include <stdbool.h>

#define VENDOR_ROCKCHIP "Rockchip"

bool rockchip_detect_cpu();
unsigned long rockchip_totalmem(unsigned long *media_mem);
void setup_hal_rockchip();

#endif /* HAL_ROCKCHIP_H */
