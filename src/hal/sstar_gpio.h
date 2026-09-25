#ifndef HAL_SSTAR_GPIO_H
#define HAL_SSTAR_GPIO_H

#include <stdbool.h>
#include <stdint.h>

/* Bit layout of the per-pad register, as the vendor driver uses it. */
#define SSTAR_GPIO_BIT_IN 0x01
#define SSTAR_GPIO_BIT_OUT 0x02
#define SSTAR_GPIO_BIT_INPUT 0x04

bool sstar_gpio_supported(void);
int sstar_gpio_num_pads(void);

/* Physical address of the pad's register, or 0 when there is no table for the
 * current chip family or the pad is out of range. */
uint32_t sstar_gpio_pad_addr(int pad);

/* The one window every pad register of the chip sits in, from the page the
 * first pad lives on to the end of the last pad's slot: what a daemon has to
 * have mapped to be holding the pads. False when there is no table. */
bool sstar_gpio_window(uint32_t *base, uint32_t *len);

/* One pad's register through /dev/mem. Reads take the low byte of the 16-bit
 * slot; writes read-modify-write it and leave the upper byte alone, because a
 * wider store reaches RIU bytes that are not mapped. Serialise the callers:
 * mem_reg() is single-threaded by contract. */
bool sstar_gpio_read(int pad, uint8_t *val);
bool sstar_gpio_write(int pad, uint8_t val);

#endif /* HAL_SSTAR_GPIO_H */
