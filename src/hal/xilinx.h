#ifndef HAL_XILINX_H
#define HAL_XILINX_H

#include <stdbool.h>

bool xilinx_detect_cpu(char *chip_name);
void xilinx_setup_hal();

#endif /* HAL_XILINX_H */
