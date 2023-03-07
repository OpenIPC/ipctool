#ifndef HAL_ALLWINNER_H
#define HAL_ALLWINNER_H

#include <stdbool.h>

bool allwinner_detect_cpu();
unsigned long allwinner_totalmem(unsigned long *media_mem);
void allwinner_setup_hal();

#endif /* HAL_ALLWINNER_H */
