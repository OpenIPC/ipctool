#ifndef HAL_SSTAR_H
#define HAL_SSTAR_H

#include <stdbool.h>

#define VENDOR_SSTAR "SStar"

bool sstar_detect_cpu();
unsigned long sstar_totalmem(unsigned long *media_mem);

#endif /* HAL_SSTAR_H */
