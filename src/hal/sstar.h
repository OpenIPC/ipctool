#ifndef HAL_SSTAR_H
#define HAL_SSTAR_H

#include <stdbool.h>

#define INFINITY3 0xC2
#define INFINITY5 0xED
#define MERCURY5 0xEE
#define INFINITY6 0xEF
#define INFINITY2M 0xF0
#define INFINITY6E 0xF1
#define INFINITY6B 0xF2
#define PIONEER3 0xF5

bool mstar_detect_cpu(char *chip_name);
bool sstar_detect_cpu(char *chip_name);
unsigned long sstar_totalmem(unsigned long *media_mem);
void sstar_setup_hal();

#endif /* HAL_SSTAR_H */
