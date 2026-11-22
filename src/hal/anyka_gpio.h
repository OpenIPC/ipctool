#ifndef HAL_ANYKA_GPIO_H
#define HAL_ANYKA_GPIO_H

#include <stdbool.h>
#include <stdint.h>

/* The AK3918AV100-family GPIO controller (AV100, AV100X, EV300L). Pads are
 * numbered as the kernel numbers them, GPIO0..GPIO104; each bank is one
 * 32-bit word per 32 pads. */
#define AK_GPIO_BASE 0x20170000u
#define AK_GPIO_DIR 0x00 /* 1: output */
#define AK_GPIO_OUT 0x14 /* the level an output drives */
#define AK_GPIO_IN 0x28  /* the level an input reads */
#define AK_GPIO_NPADS 105

bool anyka_gpio_supported(void);

/* Whether `pad` is one this package has and the controller drives: in the
 * pad-mux table, and not one of the power-management block's GPIO80/81. */
bool anyka_gpio_valid(int pad);

/* The register of `pad` in one bank, 0 when there is none. */
uint32_t anyka_gpio_reg(int pad, unsigned bank);

/* The window a daemon has to have mapped to be holding the pads. */
bool anyka_gpio_window(uint32_t *base, uint32_t *len);

/* One pad through /dev/mem, as the kernel's ak_gpio_get/ak_gpio_set do: an
 * output's level is OUT, an input's is IN. Writing sets the level first and
 * then makes the pad an output, as direction_output does, so it never drives
 * the old level for a moment. The other 31 pads of a word are read back and
 * written unchanged; serialise the callers, mem_reg() is single-threaded by
 * contract. */
bool anyka_gpio_read(int pad, bool *output, bool *level);
bool anyka_gpio_write(int pad, bool level);

#endif /* HAL_ANYKA_GPIO_H */
