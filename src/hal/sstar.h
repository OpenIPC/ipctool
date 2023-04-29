#ifndef HAL_SSTAR_H
#define HAL_SSTAR_H

#include <stdbool.h>

#define INFINITY5 0xED
#define INFINITY6 0xEF
#define INFINITY6E 0xF1
#define INFINITY6B 0xF2

bool mstar_detect_cpu(char *chip_name);
bool sstar_detect_cpu(char *chip_name);
unsigned long sstar_totalmem(unsigned long *media_mem);
void sstar_setup_hal();

#endif /* HAL_SSTAR_H */
