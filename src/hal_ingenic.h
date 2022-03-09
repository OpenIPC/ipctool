#ifndef HAL_INGENIC_H
#define HAL_INGENIC_H

#include <stdbool.h>

#define VENDOR_INGENIC "Ingenic"

bool ingenic_detect_cpu();
unsigned long ingenic_totalmem(unsigned long *media_mem);
void setup_hal_ingenic();

#endif /* HAL_INGENIC_H */
