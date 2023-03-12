#ifndef HAL_SSTAR_H
#define HAL_SSTAR_H

#include <stdbool.h>

#define INFITITY6 0xEF
#define INFITITY6B0 0xF2
#define INFITITY6E 0xF1

bool mstar_detect_cpu(char *chip_name);
bool sstar_detect_cpu(char *chip_name);
unsigned long sstar_totalmem(unsigned long *media_mem);
void sstar_setup_hal();

#endif /* HAL_SSTAR_H */
