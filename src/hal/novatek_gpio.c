/* The NA51089 GPIO controller, as the vendor's gpio-nvt-na51089.c drives it.
 *
 * Four banks of eight words at 0xF0070000: DATA, DIR, SET and CLR, one word
 * per pad group, so a pad's bit is its Linux GPIO number modulo 32 in word
 * number / 32. SET and CLR are write-one aliases, which is how the kernel
 * changes a level without a read-modify-write of the other pads; only DIR is
 * read-modified-written, under the kernel's spinlock there and under the
 * single-threaded contract of mem_reg() here.
 *
 * The per-group counts are nvt-gpio.h's *_GPIO_NUM: what the package bonds
 * out, and so what the gpiochip accepts. */

#include "hal/novatek_gpio.h"

#include "chipid.h"
#include "hal/novatek.h"
#include "tools.h"

static const uint8_t group_pads[8] = {
    23, /* C_GPIO, the "MC" pads */
    26, /* P_GPIO */
    9,  /* S_GPIO */
    10, /* L_GPIO */
    8,  /* D_GPIO */
    12, /* H_GPIO, the HSI pads */
    3,  /* A_GPIO */
    11, /* DSI_GPIO */
};

bool novatek_gpio_supported(void) { return chip_generation == CHIP_NA51089; }

bool novatek_gpio_valid(int pad) {
    if (!novatek_gpio_supported() || pad < 0 || pad >= NVT_GPIO_NPADS)
        return false;
    return (pad & 31) < group_pads[pad >> 5];
}

uint32_t novatek_gpio_reg(int pad, unsigned bank) {
    if (!novatek_gpio_valid(pad) || bank > NVT_GPIO_CLR || bank % 0x20)
        return 0;
    return NVT_GPIO_BASE + bank + (uint32_t)(pad >> 5) * 4;
}

bool novatek_gpio_window(uint32_t *base, uint32_t *len) {
    if (!novatek_gpio_supported())
        return false;
    *base = NVT_GPIO_BASE;
    *len = NVT_GPIO_CLR + 0x20;
    return true;
}

bool novatek_gpio_read(int pad, bool *output, bool *level) {
    uint32_t dir, data;
    if (!novatek_gpio_valid(pad) ||
        !mem_reg(novatek_gpio_reg(pad, NVT_GPIO_DIR), &dir, OP_READ) ||
        !mem_reg(novatek_gpio_reg(pad, NVT_GPIO_DATA), &data, OP_READ))
        return false;

    uint32_t bit = 1u << (pad & 31);
    *output = (dir & bit) != 0;
    *level = (data & bit) != 0;
    return true;
}

bool novatek_gpio_write(int pad, bool level) {
    uint32_t dir;
    uint32_t bit = 1u << (pad & 31);
    if (!novatek_gpio_valid(pad) ||
        !mem_reg(novatek_gpio_reg(pad, NVT_GPIO_DIR), &dir, OP_READ))
        return false;
    if (!(dir & bit)) {
        dir |= bit;
        if (!mem_reg(novatek_gpio_reg(pad, NVT_GPIO_DIR), &dir, OP_WRITE))
            return false;
    }
    return mem_reg(novatek_gpio_reg(pad, level ? NVT_GPIO_SET : NVT_GPIO_CLR),
                   &bit, OP_WRITE);
}
