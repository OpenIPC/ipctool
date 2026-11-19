#ifndef HAL_AXERA_H
#define HAL_AXERA_H

#include <stdbool.h>
#include <stdint.h>

/* chip_generation values, one per die family OpenIPC builds for. */
#define AXERA_AX620E 0x620E0000
#define AXERA_AX620U 0x62000000

bool axera_detect_cpu(char *chip_name);
void axera_setup_hal();

#endif /* HAL_AXERA_H */
