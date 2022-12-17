#ifndef HAL_NOVATEK_H
#define HAL_NOVATEK_H

#include <stdbool.h>

#define VENDOR_NOVATEK "Novatek"

bool novatek_detect_cpu(char *chip_name);
unsigned long novatek_totalmem(unsigned long *media_mem);
void novatek_setup_hal();

#endif /* HAL_NOVATEK_H */
