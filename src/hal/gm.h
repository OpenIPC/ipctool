#ifndef HAL_GM_H
#define HAL_GM_H

#include <stdbool.h>

bool gm_detect_cpu(char *chip_name);
#ifndef STANDALONE_LIBRARY
bool gm_dt_detect_cpu(char *chip_name);
#endif
unsigned long gm_totalmem(unsigned long *media_mem);
void gm_setup_hal();

#endif /* HAL_GM_H */
