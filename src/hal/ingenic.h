#ifndef HAL_INGENIC_H
#define HAL_INGENIC_H

#include <stdbool.h>

bool ingenic_detect_cpu(char *chip_name);
unsigned long ingenic_totalmem(unsigned long *media_mem);
void setup_hal_ingenic();

#endif /* HAL_INGENIC_H */
