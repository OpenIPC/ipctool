#ifndef HAL_XILINX_H
#define HAL_XILINX_H

#include <stdbool.h>

bool xilinx_detect_cpu(char *chip_name);
void setup_hal_xilinx();

#endif /* HAL_XILINX_H */
