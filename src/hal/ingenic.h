#ifndef HAL_INGENIC_H
#define HAL_INGENIC_H

#include <stdbool.h>

#define T10 0x10
#define T20 0x20
#define T21 0x21
#define T30 0x30
#define T31 0x31
#define T40 0x40
#define T41 0x41

bool ingenic_detect_cpu(char *chip_name);
unsigned long ingenic_totalmem(unsigned long *media_mem);
void setup_hal_ingenic();

#endif /* HAL_INGENIC_H */
