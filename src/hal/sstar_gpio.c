/* SigmaStar per-pad GPIO registers.
 *
 * Where HiSilicon banks eight pins behind one data and one direction word,
 * SigmaStar's Infinity6C gives every pad its own one-byte register: bit 0 is
 * the pin level, bit 1 the output value, and bit 2 the output enable, which
 * on this family reads 1 while the pad drives Hi-Z (an input) and 0 while it
 * drives. Like the pad-mux ports these registers are 16-bit slots in
 * four-byte RIU holes, so they are reached with the 16-bit accessors and a
 * 32-bit store would write two bytes that do not exist.
 *
 * The table below is the one the vendor's own driver carries
 * (drivers/sstar/gpio/infinity6c/mhal_gpio.c in the SigmaStar kernel, GPL-2)
 * converted from RIU byte offset r to physical 0x1F207C00 + 2 * r, and the
 * result was measured against a live Infinity6C board: writing pads 12 and 30
 * through sysfs moves their bytes at exactly these addresses, and the idle
 * levels of pads 10, 23 and 40/41 read back the states their exporters had
 * left. The four PAD_ETH_* pads the vendor adds by hand sit outside the
 * gpiochip range and are not listed. */

#include "hal/sstar_gpio.h"

#include <string.h>

#include "chipid.h"
#include "hal/sstar.h"
#include "tools.h"

static const uint32_t i6c_gpio_regs[] = {
    0x1F207C00, 0x1F207C04, 0x1F207C08, 0x1F207C0C, 0x1F207C10, 0x1F207C14,
    0x1F207C18, 0x1F207C1C, 0x1F207C20, 0x1F207C24, 0x1F207C28, 0x1F207C2C,
    0x1F207C30, 0x1F207C34, 0x1F207C38, 0x1F207C3C, 0x1F207C40, 0x1F207C44,
    0x1F207C48, 0x1F207C4C, 0x1F207C50, 0x1F207C54, 0x1F207C58, 0x1F207C5C,
    0x1F207C64, 0x1F207C68, 0x1F207C6C, 0x1F207C70, 0x1F207C74, 0x1F207C78,
    0x1F207C7C, 0x1F207C80, 0x1F207C84, 0x1F207C88, 0x1F207C8C, 0x1F207C90,
    0x1F207C94, 0x1F207C98, 0x1F207C9C, 0x1F207CA0, 0x1F207CA4, 0x1F207CA8,
    0x1F207CC4, 0x1F207CC8, 0x1F207CCC, 0x1F207CD0, 0x1F207CD4, 0x1F207CD8,
    0x1F207CDC, 0x1F207CE0, 0x1F207CE4, 0x1F207CE8, 0x1F207CEC, 0x1F207CF0,
    0x1F207CF4, 0x1F207CF8, 0x1F207CFC, 0x1F207D00, 0x1F207D04, 0x1F207D08,
    0x1F207D0C, 0x1F207D10, 0x1F207D14, 0x1F207D18, 0x1F207D1C, 0x1F207D20,
    0x1F207D24, 0x1F207D28, 0x1F207D2C, 0x1F207D30, 0x1F207D34, 0x1F207D38,
    0x1F207D3C, 0x1F207D40, 0x1F207D44, 0x1F207D48, 0x1F207D4C, 0x1F207D50,
    0x1F207D54, 0x1F207D58, 0x1F207D5C, 0x1F207D60,
};

bool sstar_gpio_supported(void) {
    switch (chip_generation) {
    case INFINITY6C:
        return true;
    default:
        return false;
    }
}

int sstar_gpio_num_pads(void) {
    if (!sstar_gpio_supported())
        return 0;
    return (int)(sizeof(i6c_gpio_regs) / sizeof(i6c_gpio_regs[0]));
}

uint32_t sstar_gpio_pad_addr(int pad) {
    if (pad < 0 || pad >= sstar_gpio_num_pads())
        return 0;
    return i6c_gpio_regs[pad];
}

bool sstar_gpio_read(int pad, uint8_t *val) {
    uint32_t addr = sstar_gpio_pad_addr(pad);
    if (!addr)
        return false;
    uint32_t data;
    if (!mem_reg(addr, &data, OP_READ_16))
        return false;
    *val = (uint8_t)(data & 0xff);
    return true;
}

bool sstar_gpio_write(int pad, uint8_t val) {
    uint32_t addr = sstar_gpio_pad_addr(pad);
    if (!addr)
        return false;
    uint32_t data;
    if (!mem_reg(addr, &data, OP_READ_16))
        return false;
    data = (data & 0xff00) | val;
    return mem_reg(addr, &data, OP_WRITE_16);
}
