#ifndef HAL_NOVATEK_REGINFO_H
#define HAL_NOVATEK_REGINFO_H

#include "reginfo.h"

/* What plain `reginfo` dumps on a NA51089, NA51055 or NA51084: the TOP block's
 * pad-mux state, whole. The first eleven are the per-peripheral location fields
 * -- REG5 holds I2C, SPI, SIF and ETH, REG9 the UARTs, and so on; the vendor's
 * plat-na51089/top_reg.h has the bit layout. The last eight are the per-pad
 * gate bitmaps, one per pad group, where a set bit keeps the pad a GPIO.
 * `reginfo --pads` is the same state read pad by pad. */
MUXCTRL(NA51089_reg0, 0xF0010000, PMX_TOP_REG0)
MUXCTRL(NA51089_reg1, 0xF0010004, PMX_TOP_REG1)
MUXCTRL(NA51089_reg2, 0xF0010008, PMX_TOP_REG2)
MUXCTRL(NA51089_reg3, 0xF001000C, PMX_TOP_REG3)
MUXCTRL(NA51089_reg4, 0xF0010010, PMX_TOP_REG4)
MUXCTRL(NA51089_reg5, 0xF0010014, PMX_TOP_REG5)
MUXCTRL(NA51089_reg6, 0xF0010018, PMX_TOP_REG6)
MUXCTRL(NA51089_reg7, 0xF001001C, PMX_TOP_REG7)
MUXCTRL(NA51089_reg8, 0xF0010020, PMX_TOP_REG8)
MUXCTRL(NA51089_reg9, 0xF0010024, PMX_TOP_REG9)
MUXCTRL(NA51089_reg10, 0xF0010028, PMX_TOP_REG10)
MUXCTRL(NA51089_reg11, 0xF00100A0, PMX_TOP_CGPIO)
MUXCTRL(NA51089_reg12, 0xF00100A8, PMX_TOP_PGPIO)
MUXCTRL(NA51089_reg13, 0xF00100B0, PMX_TOP_SGPIO)
MUXCTRL(NA51089_reg14, 0xF00100B8, PMX_TOP_LGPIO)
MUXCTRL(NA51089_reg15, 0xF00100D0, PMX_TOP_DGPIO)
MUXCTRL(NA51089_reg16, 0xF00100D8, PMX_TOP_HGPIO)
MUXCTRL(NA51089_reg17, 0xF00100E0, PMX_TOP_AGPIO)
MUXCTRL(NA51089_reg18, 0xF00100E8, PMX_TOP_DSIGPIO)

static const muxctrl_reg_t *NA51089_regs[] = {
    &NA51089_reg0,  &NA51089_reg1,  &NA51089_reg2,  &NA51089_reg3,
    &NA51089_reg4,  &NA51089_reg5,  &NA51089_reg6,  &NA51089_reg7,
    &NA51089_reg8,  &NA51089_reg9,  &NA51089_reg10, &NA51089_reg11,
    &NA51089_reg12, &NA51089_reg13, &NA51089_reg14, &NA51089_reg15,
    &NA51089_reg16, &NA51089_reg17, &NA51089_reg18, 0};

/* NA51055 and NA51084: the same block with no DSI pad group. */
static const muxctrl_reg_t *NA51055_regs[] = {&NA51089_reg0,
                                              &NA51089_reg1,
                                              &NA51089_reg2,
                                              &NA51089_reg3,
                                              &NA51089_reg4,
                                              &NA51089_reg5,
                                              &NA51089_reg6,
                                              &NA51089_reg7,
                                              &NA51089_reg8,
                                              &NA51089_reg9,
                                              &NA51089_reg10,
                                              &NA51089_reg11,
                                              &NA51089_reg12,
                                              &NA51089_reg13,
                                              &NA51089_reg14,
                                              &NA51089_reg15,
                                              &NA51089_reg16,
                                              &NA51089_reg17,
                                              0};

#endif /* HAL_NOVATEK_REGINFO_H */
