#ifndef HAL_NOVATEK_GPIO_H
#define HAL_NOVATEK_GPIO_H

#include <stdbool.h>
#include <stdint.h>

/* The NA51089 GPIO controller. Pads are numbered as the kernel numbers them:
 * C_GPIO(n) is n, P_GPIO(n) 0x20 + n, and so on in steps of 0x20 through S,
 * L, D, H, A and DSI. Every group is one 32-bit word in each bank. */
#define NVT_GPIO_BASE 0xF0070000u
#define NVT_GPIO_DATA 0x00 /* the pin's level */
#define NVT_GPIO_DIR 0x20  /* 1: output */
#define NVT_GPIO_SET 0x40  /* write 1 to drive high */
#define NVT_GPIO_CLR 0x60  /* write 1 to drive low */
#define NVT_GPIO_NPADS 0x100

bool novatek_gpio_supported(void);

/* Whether the kernel's gpiochip answers to `pad`: the package bonds it out.
 * The holes between groups are not pads. */
bool novatek_gpio_valid(int pad);

/* The register of `pad` in one bank, 0 when there is none. */
uint32_t novatek_gpio_reg(int pad, unsigned bank);

/* The window a daemon has to have mapped to be holding the pads. */
bool novatek_gpio_window(uint32_t *base, uint32_t *len);

/* One pad through /dev/mem. Writing makes the pad an output first, as the
 * kernel's direction_output does, then drives it through SET or CLR so the
 * other 31 pads of the word are never written. Serialise the callers:
 * mem_reg() is single-threaded by contract. */
bool novatek_gpio_read(int pad, bool *output, bool *level);
bool novatek_gpio_write(int pad, bool level);

#endif /* HAL_NOVATEK_GPIO_H */
