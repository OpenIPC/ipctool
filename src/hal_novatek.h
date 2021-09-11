#ifndef HAL_NOVATEK_H
#define HAL_NOVATEK_H

#include <stdbool.h>

#define VENDOR_NOVATEK "Novatek"

bool novatek_detect_cpu();
unsigned long novatek_totalmem(unsigned long *media_mem);
void setup_hal_novatek();

#endif /* HAL_NOVATEK_H */
