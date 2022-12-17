#ifndef HAL_GM_H
#define HAL_GM_H

#include <stdbool.h>

#define VENDOR_GM "Grain-Media"

bool gm_detect_cpu(char *chip_name);
unsigned long gm_totalmem(unsigned long *media_mem);
void gm_setup_hal();

#endif /* HAL_GM_H */
