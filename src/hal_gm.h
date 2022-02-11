#ifndef HAL_GM_H
#define HAL_GM_H

#include <stdbool.h>

#define VENDOR_GM "Grain-Media"

bool gm_detect_cpu();
unsigned long gm_totalmem(unsigned long *media_mem);
void setup_hal_gm();

#endif /* HAL_GM_H */
