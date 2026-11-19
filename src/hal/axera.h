#ifndef HAL_AXERA_H
#define HAL_AXERA_H

#include <stdbool.h>
#include <stdint.h>

/* chip_generation values, one per die family OpenIPC builds for. */
#define AXERA_AX620E 0x620E0000
#define AXERA_AX620U 0x62000000

bool axera_detect_cpu(char *chip_name);
void axera_setup_hal();

/* The decoding, without the register reads, for axera_test.c. */
int axera_hamming74_decode(uint8_t code);
const char *axera_ax620e_name(uint32_t efuse_blk2, uint32_t dummy_sw9);
const char *axera_ax620u_name(uint32_t ddrmc_2068, uint32_t bond_opt);

#endif /* HAL_AXERA_H */
