/* The NA51089, NA51055 and NA51084 GPIO controller, as the vendor's
 * gpio-nvt-na51089.c and gpio-nvt-na51055.c drive it: the same block at the
 * same address on all three.
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

/* C ("MC"), P, S, L, D, H (HSI), A, DSI. */
static const uint8_t na51089_pads[8] = {23, 26, 9, 10, 8, 12, 3, 11};
/* NA51055 and NA51084: more S, L and D pads, and no DSI group. */
static const uint8_t na51055_pads[8] = {23, 26, 13, 25, 11, 12, 3, 0};

static const uint8_t *group_pads(void) {
    switch (chip_generation) {
    case CHIP_NA51089:
        return na51089_pads;
    case CHIP_NA51055:
    case CHIP_NA51084:
        return na51055_pads;
    default:
        return NULL;
    }
}

bool novatek_gpio_supported(void) { return group_pads() != NULL; }

bool novatek_gpio_valid(int pad) {
    const uint8_t *pads = group_pads();
    if (pads == NULL || pad < 0 || pad >= NVT_GPIO_NPADS)
        return false;
    return (pad & 31) < pads[pad >> 5];
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
