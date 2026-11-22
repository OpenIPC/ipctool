#ifndef HAL_ANYKA_GPIO_H
#define HAL_ANYKA_GPIO_H

#include <stdbool.h>
#include <stddef.h>
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

/* Who in the kernel holds `pad`, written to `who` (a device or consumer
 * name), or false when nothing visible does. Asked of three places, any of
 * which may be missing: the pinctrl mux owner in debugfs
 * (pinctrl/<dev>/pinmux-pins: pad 23 is "20300000.ethernet" while muxed as
 * a GPIO), the GPIOs the kernel has handed to a driver (debugfs gpio:
 * "sensor-reset", "switch0_gpio"; a pad the user exported through sysfs is
 * not counted), and the *-gpio / *-gpios properties of the enabled
 * device-tree nodes (the PHY's reset-gpios, GPIO71 on #380's camera, which
 * the MAC driver releases after its reset pulse). Driving such a pad takes
 * away what uses it: the network, the SD card, the sensor. */
bool anyka_gpio_owner(int pad, char *who, size_t len);

/* A directory standing in for / in anyka_gpio_owner(), or NULL for /. A
 * test seam, not API. */
void anyka_gpio_set_root(const char *root);

#endif /* HAL_ANYKA_GPIO_H */
