#ifndef HAL_SSTAR_H
#define HAL_SSTAR_H

#include <stdbool.h>

#define VENDOR_SSTAR "SStar"
#define VENDOR_MSTAR "MStar"

bool mstar_detect_cpu(char *chip_name);
bool sstar_detect_cpu(char *chip_name);
unsigned long sstar_totalmem(unsigned long *media_mem);
void sstar_setup_hal();

#endif /* HAL_SSTAR_H */
