/* The AK3918AV100-family GPIO controller, as the vendor's
 * drivers/pinctrl/anyka/ak_3918av100_pinctrl.c drives it: at 0x20170000,
 * banks of four words -- DIR at +0x00, OUT at +0x14, IN at +0x28 -- and a
 * pad's bit is its number modulo 32 in word number / 32.
 *
 * The block has no set/clear aliases, so a level change is a read-modify-
 * write of the whole word, as the kernel does it (under local_irq_save
 * there, under the single-threaded contract of mem_reg() here).
 *
 * Which pads exist is the pad-mux table's to say: the EV300L package bonds
 * out 59 of the AV100's 93. */

#include "hal/anyka_gpio.h"

#include "chipid.h"
#include "hal/anyka.h"
#include "ipchw.h"
#include "tools.h"

bool anyka_gpio_supported(void) {
    return chip_generation == AK39_AV100 || chip_generation == AK39_EV300L;
}

bool anyka_gpio_valid(int pad) {
    if (!anyka_gpio_supported() || pad < 0 || pad >= AK_GPIO_NPADS)
        return false;
    ipchw_padmux_t row;
    return ipchw_padmux_by_pad(pad, &row, 1) > 0;
}

uint32_t anyka_gpio_reg(int pad, unsigned bank) {
    if (!anyka_gpio_valid(pad) ||
        (bank != AK_GPIO_DIR && bank != AK_GPIO_OUT && bank != AK_GPIO_IN))
        return 0;
    return AK_GPIO_BASE + bank + (uint32_t)(pad >> 5) * 4;
}

bool anyka_gpio_window(uint32_t *base, uint32_t *len) {
    if (!anyka_gpio_supported())
        return false;
    *base = AK_GPIO_BASE;
    *len = AK_GPIO_IN + 4 * ((AK_GPIO_NPADS + 31) / 32);
    return true;
}

bool anyka_gpio_read(int pad, bool *output, bool *level) {
    uint32_t dir, data;
    if (!anyka_gpio_valid(pad) ||
        !mem_reg(anyka_gpio_reg(pad, AK_GPIO_DIR), &dir, OP_READ))
        return false;
    uint32_t bit = 1u << (pad & 31);
    *output = (dir & bit) != 0;
    if (!mem_reg(anyka_gpio_reg(pad, *output ? AK_GPIO_OUT : AK_GPIO_IN),
                 &data, OP_READ))
        return false;
    *level = (data & bit) != 0;
    return true;
}

bool anyka_gpio_write(int pad, bool level) {
    uint32_t out, dir;
    uint32_t bit = 1u << (pad & 31);
    if (!anyka_gpio_valid(pad) ||
        !mem_reg(anyka_gpio_reg(pad, AK_GPIO_OUT), &out, OP_READ))
        return false;
    out = level ? out | bit : out & ~bit;
    if (!mem_reg(anyka_gpio_reg(pad, AK_GPIO_OUT), &out, OP_WRITE) ||
        !mem_reg(anyka_gpio_reg(pad, AK_GPIO_DIR), &dir, OP_READ))
        return false;
    if (dir & bit)
        return true;
    dir |= bit;
    return mem_reg(anyka_gpio_reg(pad, AK_GPIO_DIR), &dir, OP_WRITE);
}
