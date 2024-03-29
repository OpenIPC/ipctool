#ifndef HAL_SSTAR_REGINFO_H
#define HAL_SSTAR_REGINFO_H

#include "reginfo.h"

MUXCTRL(I6B_reg0, 0x1F207800, "PAD_GPIO0")
MUXCTRL(I6B_reg1, 0x1F207804, "PAD_GPIO1")
MUXCTRL(I6B_reg2, 0x1F207808, "PAD_GPIO2")
MUXCTRL(I6B_reg3, 0x1F20780C, "PAD_GPIO3")
MUXCTRL(I6B_reg4, 0x1F207810, "PAD_GPIO4")
MUXCTRL(I6B_reg5, 0x1F207814, "PAD_GPIO5")
MUXCTRL(I6B_reg6, 0x1F207818, "PAD_GPIO6")
MUXCTRL(I6B_reg7, 0x1F20781C, "PAD_GPIO7")
MUXCTRL(I6B_reg8, 0x1F207820, "PAD_GPIO8")
MUXCTRL(I6B_reg9, 0x1F207824, "PAD_GPIO9")
MUXCTRL(I6B_reg10, 0x1F207830, "PAD_GPIO12")
MUXCTRL(I6B_reg11, 0x1F207834, "PAD_GPIO13")
MUXCTRL(I6B_reg12, 0x1F207838, "PAD_GPIO14")
MUXCTRL(I6B_reg13, 0x1F20783C, "PAD_GPIO15")
MUXCTRL(I6B_reg14, 0x1F207850, "PAD_FUART_RX")
MUXCTRL(I6B_reg15, 0x1F207854, "PAD_FUART_TX")
MUXCTRL(I6B_reg16, 0x1F207858, "PAD_FUART_CTS")
MUXCTRL(I6B_reg17, 0x1F20785C, "PAD_FUART_RTS")
MUXCTRL(I6B_reg18, 0x1F207980, "PAD_I2C0_SCL")
MUXCTRL(I6B_reg19, 0x1F207984, "PAD_I2C0_SDA")
MUXCTRL(I6B_reg20, 0x1F207988, "PAD_I2C1_SCL")
MUXCTRL(I6B_reg21, 0x1F20798C, "PAD_I2C1_SDA")
MUXCTRL(I6B_reg22, 0x1F207880, "PAD_SR_IO00")
MUXCTRL(I6B_reg23, 0x1F207884, "PAD_SR_IO01")
MUXCTRL(I6B_reg24, 0x1F207888, "PAD_SR_IO02")
MUXCTRL(I6B_reg25, 0x1F20788C, "PAD_SR_IO03")
MUXCTRL(I6B_reg26, 0x1F207890, "PAD_SR_IO04")
MUXCTRL(I6B_reg27, 0x1F207894, "PAD_SR_IO05")
MUXCTRL(I6B_reg28, 0x1F207898, "PAD_SR_IO06")
MUXCTRL(I6B_reg29, 0x1F20789C, "PAD_SR_IO07")
MUXCTRL(I6B_reg30, 0x1F2078A0, "PAD_SR_IO08")
MUXCTRL(I6B_reg31, 0x1F2078A4, "PAD_SR_IO09")
MUXCTRL(I6B_reg32, 0x1F2078A8, "PAD_SR_IO10")
MUXCTRL(I6B_reg33, 0x1F2078AC, "PAD_SR_IO11")
MUXCTRL(I6B_reg34, 0x1F2078B0, "PAD_SR_IO12")
MUXCTRL(I6B_reg35, 0x1F2078B4, "PAD_SR_IO13")
MUXCTRL(I6B_reg36, 0x1F2078B8, "PAD_SR_IO14")
MUXCTRL(I6B_reg37, 0x1F2078BC, "PAD_SR_IO15")
MUXCTRL(I6B_reg38, 0x1F2078C0, "PAD_SR_IO16")
MUXCTRL(I6B_reg39, 0x1F2078C4, "PAD_SR_IO17")
MUXCTRL(I6B_reg40, 0x1F207860, "PAD_UART0_RX")
MUXCTRL(I6B_reg41, 0x1F207864, "PAD_UART0_TX")
MUXCTRL(I6B_reg42, 0x1F207868, "PAD_UART1_RX")
MUXCTRL(I6B_reg43, 0x1F20786C, "PAD_UART1_TX")
MUXCTRL(I6B_reg44, 0x1F2079C0, "PAD_SPI0_CZ")
MUXCTRL(I6B_reg45, 0x1F2079C4, "PAD_SPI0_CK")
MUXCTRL(I6B_reg46, 0x1F2079C8, "PAD_SPI0_DI")
MUXCTRL(I6B_reg47, 0x1F2079CC, "PAD_SPI0_DO")
MUXCTRL(I6B_reg48, 0x1F2079D0, "PAD_SPI1_CZ")
MUXCTRL(I6B_reg49, 0x1F2079D4, "PAD_SPI1_CK")
MUXCTRL(I6B_reg50, 0x1F2079D8, "PAD_SPI1_DI")
MUXCTRL(I6B_reg51, 0x1F2079DC, "PAD_SPI1_DO")
MUXCTRL(I6B_reg52, 0x1F207840, "PAD_PWM0")
MUXCTRL(I6B_reg53, 0x1F207844, "PAD_PWM1")
MUXCTRL(I6B_reg54, 0x1F207940, "PAD_SD_CLK")
MUXCTRL(I6B_reg55, 0x1F207944, "PAD_SD_CMD")
MUXCTRL(I6B_reg56, 0x1F207948, "PAD_SD_D0")
MUXCTRL(I6B_reg57, 0x1F20794C, "PAD_SD_D1")
MUXCTRL(I6B_reg58, 0x1F207950, "PAD_SD_D2")
MUXCTRL(I6B_reg59, 0x1F207954, "PAD_SD_D3")
MUXCTRL(I6B_reg60, 0x1F001F1C, "PAD_PM_SD_CDZ")
MUXCTRL(I6B_reg61, 0x1F001E50, "PAD_PM_IRIN")
MUXCTRL(I6B_reg62, 0x1F001E00, "PAD_PM_GPIO0")
MUXCTRL(I6B_reg63, 0x1F001E04, "PAD_PM_GPIO1")
MUXCTRL(I6B_reg64, 0x1F001E08, "PAD_PM_GPIO2")
MUXCTRL(I6B_reg65, 0x1F001E0C, "PAD_PM_GPIO3")
MUXCTRL(I6B_reg66, 0x1F001E10, "PAD_PM_GPIO4")
MUXCTRL(I6B_reg67, 0x1F001E1C, "PAD_PM_GPIO7")
MUXCTRL(I6B_reg68, 0x1F001E20, "PAD_PM_GPIO8")
MUXCTRL(I6B_reg69, 0x1F001E24, "PAD_PM_GPIO9")
MUXCTRL(I6B_reg70, 0x1F001E60, "PAD_PM_SPI_CZ")
MUXCTRL(I6B_reg71, 0x1F001E64, "PAD_PM_SPI_CK")
MUXCTRL(I6B_reg72, 0x1F001E68, "PAD_PM_SPI_DI")
MUXCTRL(I6B_reg73, 0x1F001E6C, "PAD_PM_SPI_DO")
MUXCTRL(I6B_reg74, 0x1F001F10, "PAD_PM_SPI_WPZ")
MUXCTRL(I6B_reg75, 0x1F001F14, "PAD_PM_SPI_HLD")
MUXCTRL(I6B_reg76, 0x1F001F28, "PAD_PM_LED0")
MUXCTRL(I6B_reg77, 0x1F001F2C, "PAD_PM_LED1")
MUXCTRL(I6B_reg78, 0x1F002848, "PAD_SAR_GPIO0")
MUXCTRL(I6B_reg79, 0x1F002848, "PAD_SAR_GPIO1")
MUXCTRL(I6B_reg80, 0x1F002848, "PAD_SAR_GPIO2")
MUXCTRL(I6B_reg81, 0x1F002848, "PAD_SAR_GPIO3")
MUXCTRL(I6B_reg82, 0x1F0067C8, "PAD_ETH_RN")
MUXCTRL(I6B_reg83, 0x1F0067C8, "PAD_ETH_RP")
MUXCTRL(I6B_reg84, 0x1F0067C8, "PAD_ETH_TN")
MUXCTRL(I6B_reg85, 0x1F0067C8, "PAD_ETH_TP")
MUXCTRL(I6B_reg86, 0x1F284214, "PAD_USB_DM")
MUXCTRL(I6B_reg87, 0x1F284214, "PAD_USB_DP")
MUXCTRL(I6B_reg88, 0x1F207900, "PAD_SD1_IO0")
MUXCTRL(I6B_reg89, 0x1F207904, "PAD_SD1_IO1")
MUXCTRL(I6B_reg90, 0x1F207908, "PAD_SD1_IO2")
MUXCTRL(I6B_reg91, 0x1F20790C, "PAD_SD1_IO3")
MUXCTRL(I6B_reg92, 0x1F207910, "PAD_SD1_IO4")
MUXCTRL(I6B_reg93, 0x1F207914, "PAD_SD1_IO5")
MUXCTRL(I6B_reg94, 0x1F207918, "PAD_SD1_IO6")
MUXCTRL(I6B_reg95, 0x1F20791C, "PAD_SD1_IO7")
MUXCTRL(I6B_reg96, 0x1F207920, "PAD_SD1_IO8")

static const muxctrl_reg_t *I6B_regs[] = {
    &I6B_reg0,  &I6B_reg1,
    &I6B_reg2,  &I6B_reg3,
    &I6B_reg4,  &I6B_reg5,
    &I6B_reg6,  &I6B_reg7,
    &I6B_reg8,  &I6B_reg9,
    &I6B_reg10, &I6B_reg11,
    &I6B_reg12, &I6B_reg13,
    &I6B_reg14, &I6B_reg15,
    &I6B_reg16, &I6B_reg17,
    &I6B_reg18, &I6B_reg19,
    &I6B_reg20, &I6B_reg21,
    &I6B_reg22, &I6B_reg23,
    &I6B_reg24, &I6B_reg25,
    &I6B_reg26, &I6B_reg27,
    &I6B_reg28, &I6B_reg29,
    &I6B_reg30, &I6B_reg31,
    &I6B_reg32, &I6B_reg33,
    &I6B_reg34, &I6B_reg35,
    &I6B_reg36, &I6B_reg37,
    &I6B_reg38, &I6B_reg39,
    &I6B_reg40, &I6B_reg41,
    &I6B_reg42, &I6B_reg43,
    &I6B_reg44, &I6B_reg45,
    &I6B_reg46, &I6B_reg47,
    &I6B_reg48, &I6B_reg49,
    &I6B_reg50, &I6B_reg51,
    &I6B_reg52, &I6B_reg53,
    &I6B_reg54, &I6B_reg55,
    &I6B_reg56, &I6B_reg57,
    &I6B_reg58, &I6B_reg59,
    &I6B_reg60, &I6B_reg61,
    &I6B_reg62, &I6B_reg63,
    &I6B_reg64, &I6B_reg65,
    &I6B_reg66, &I6B_reg67,
    &I6B_reg68, &I6B_reg69,
    &I6B_reg70, &I6B_reg71,
    &I6B_reg72, &I6B_reg73,
    &I6B_reg74, &I6B_reg75,
    &I6B_reg76, &I6B_reg77,
    &I6B_reg78, &I6B_reg79,
    &I6B_reg80, &I6B_reg81,
    &I6B_reg82, &I6B_reg83,
    &I6B_reg84, &I6B_reg85,
    &I6B_reg86, &I6B_reg87,
    &I6B_reg88, &I6B_reg89,
    &I6B_reg90, &I6B_reg91,
    &I6B_reg92, &I6B_reg93,
    &I6B_reg94, &I6B_reg95,
    &I6B_reg96, 0,
};

MUXCTRL(I6C_reg0, 0x1F207C00, "PAD_SD1_IO1")
MUXCTRL(I6C_reg1, 0x1F207C04, "PAD_SD1_IO0")
MUXCTRL(I6C_reg2, 0x1F207C08, "PAD_SD1_IO5")
MUXCTRL(I6C_reg3, 0x1F207C0C, "PAD_SD1_IO4")
MUXCTRL(I6C_reg4, 0x1F207C10, "PAD_SD1_IO3")
MUXCTRL(I6C_reg5, 0x1F207C14, "PAD_SD1_IO2")
MUXCTRL(I6C_reg6, 0x1F207C18, "PAD_SD1_IO6")
MUXCTRL(I6C_reg7, 0x1F207C1C, "PAD_UART1_RX")
MUXCTRL(I6C_reg8, 0x1F207C20, "PAD_UART1_TX")
MUXCTRL(I6C_reg9, 0x1F207C24, "PAD_SPI0_CZ")
MUXCTRL(I6C_reg10, 0x1F207C28, "PAD_SPI0_CK")
MUXCTRL(I6C_reg11, 0x1F207C2C, "PAD_SPI0_DI")
MUXCTRL(I6C_reg12, 0x1F207C30, "PAD_SPI0_DO")
MUXCTRL(I6C_reg13, 0x1F207C34, "PAD_PWM0")
MUXCTRL(I6C_reg14, 0x1F207C38, "PAD_PWM1")
MUXCTRL(I6C_reg15, 0x1F207C3C, "PAD_SD_CLK")
MUXCTRL(I6C_reg16, 0x1F207C40, "PAD_SD_CMD")
MUXCTRL(I6C_reg17, 0x1F207C44, "PAD_SD_D0")
MUXCTRL(I6C_reg18, 0x1F207C48, "PAD_SD_D1")
MUXCTRL(I6C_reg19, 0x1F207C4C, "PAD_SD_D2")
MUXCTRL(I6C_reg20, 0x1F207C50, "PAD_SD_D3")
MUXCTRL(I6C_reg21, 0x1F207C54, "PAD_USB_CID")
MUXCTRL(I6C_reg22, 0x1F207C58, "PAD_PM_SD_CDZ")
MUXCTRL(I6C_reg23, 0x1F207C5C, "PAD_PM_IRIN")
MUXCTRL(I6C_reg24, 0x1F207C64, "PAD_PM_UART_RX")
MUXCTRL(I6C_reg25, 0x1F207C68, "PAD_PM_UART_TX")
MUXCTRL(I6C_reg26, 0x1F207C6C, "PAD_PM_GPIO0")
MUXCTRL(I6C_reg27, 0x1F207C70, "PAD_PM_GPIO1")
MUXCTRL(I6C_reg28, 0x1F207C74, "PAD_PM_GPIO2")
MUXCTRL(I6C_reg29, 0x1F207C78, "PAD_PM_GPIO3")
MUXCTRL(I6C_reg30, 0x1F207C7C, "PAD_PM_GPIO4")
MUXCTRL(I6C_reg31, 0x1F207C80, "PAD_PM_GPIO7")
MUXCTRL(I6C_reg32, 0x1F207C84, "PAD_PM_GPIO8")
MUXCTRL(I6C_reg33, 0x1F207C88, "PAD_PM_GPIO9")
MUXCTRL(I6C_reg34, 0x1F207C8C, "PAD_PM_SPI_CZ")
MUXCTRL(I6C_reg35, 0x1F207C90, "PAD_PM_SPI_DI")
MUXCTRL(I6C_reg36, 0x1F207C94, "PAD_PM_SPI_WPZ")
MUXCTRL(I6C_reg37, 0x1F207C98, "PAD_PM_SPI_DO")
MUXCTRL(I6C_reg38, 0x1F207C9C, "PAD_PM_SPI_CK")
MUXCTRL(I6C_reg39, 0x1F207CA0, "PAD_PM_SPI_HLD")
MUXCTRL(I6C_reg40, 0x1F207CA4, "PAD_PM_LED0")
MUXCTRL(I6C_reg41, 0x1F207CA8, "PAD_PM_LED1")
MUXCTRL(I6C_reg42, 0x1F207CC4, "PAD_FUART_RX")
MUXCTRL(I6C_reg43, 0x1F207CC8, "PAD_FUART_TX")
MUXCTRL(I6C_reg44, 0x1F207CCC, "PAD_FUART_CTS")
MUXCTRL(I6C_reg45, 0x1F207CD0, "PAD_FUART_RTS")
MUXCTRL(I6C_reg46, 0x1F207CD4, "PAD_GPIO0")
MUXCTRL(I6C_reg47, 0x1F207CD8, "PAD_GPIO1")
MUXCTRL(I6C_reg48, 0x1F207CDC, "PAD_GPIO2")
MUXCTRL(I6C_reg49, 0x1F207CE0, "PAD_GPIO3")
MUXCTRL(I6C_reg50, 0x1F207CE4, "PAD_GPIO4")
MUXCTRL(I6C_reg51, 0x1F207CE8, "PAD_GPIO5")
MUXCTRL(I6C_reg52, 0x1F207CEC, "PAD_GPIO6")
MUXCTRL(I6C_reg53, 0x1F207CF0, "PAD_GPIO7")
MUXCTRL(I6C_reg54, 0x1F207CF4, "PAD_GPIO14")
MUXCTRL(I6C_reg55, 0x1F207CF8, "PAD_GPIO15")
MUXCTRL(I6C_reg56, 0x1F207CFC, "PAD_I2C0_SCL")
MUXCTRL(I6C_reg57, 0x1F207D00, "PAD_I2C0_SDA")
MUXCTRL(I6C_reg58, 0x1F207D04, "PAD_I2C1_SCL")
MUXCTRL(I6C_reg59, 0x1F207D08, "PAD_I2C1_SDA")
MUXCTRL(I6C_reg60, 0x1F207D0C, "PAD_SR_IO00")
MUXCTRL(I6C_reg61, 0x1F207D10, "PAD_SR_IO01")
MUXCTRL(I6C_reg62, 0x1F207D14, "PAD_SR_IO02")
MUXCTRL(I6C_reg63, 0x1F207D18, "PAD_SR_IO03")
MUXCTRL(I6C_reg64, 0x1F207D1C, "PAD_SR_IO04")
MUXCTRL(I6C_reg65, 0x1F207D20, "PAD_SR_IO05")
MUXCTRL(I6C_reg66, 0x1F207D24, "PAD_SR_IO06")
MUXCTRL(I6C_reg67, 0x1F207D28, "PAD_SR_IO07")
MUXCTRL(I6C_reg68, 0x1F207D2C, "PAD_SR_IO08")
MUXCTRL(I6C_reg69, 0x1F207D30, "PAD_SR_IO09")
MUXCTRL(I6C_reg70, 0x1F207D34, "PAD_SR_IO10")
MUXCTRL(I6C_reg71, 0x1F207D38, "PAD_SR_IO11")
MUXCTRL(I6C_reg72, 0x1F207D3C, "PAD_SR_IO12")
MUXCTRL(I6C_reg73, 0x1F207D40, "PAD_SR_IO13")
MUXCTRL(I6C_reg74, 0x1F207D44, "PAD_SR_IO14")
MUXCTRL(I6C_reg75, 0x1F207D48, "PAD_SR_IO15")
MUXCTRL(I6C_reg76, 0x1F207D4C, "PAD_SR_IO16")
MUXCTRL(I6C_reg77, 0x1F207D50, "PAD_SR_IO17")
MUXCTRL(I6C_reg78, 0x1F207D54, "PAD_SAR_GPIO3")
MUXCTRL(I6C_reg79, 0x1F207D58, "PAD_SAR_GPIO2")
MUXCTRL(I6C_reg80, 0x1F207D5C, "PAD_SAR_GPIO1")
MUXCTRL(I6C_reg81, 0x1F207D60, "PAD_SAR_GPIO0")

static const muxctrl_reg_t *I6C_regs[] = {
    &I6C_reg0,  &I6C_reg1,
    &I6C_reg2,  &I6C_reg3,
    &I6C_reg4,  &I6C_reg5,
    &I6C_reg6,  &I6C_reg7,
    &I6C_reg8,  &I6C_reg9,
    &I6C_reg10, &I6C_reg11,
    &I6C_reg12, &I6C_reg13,
    &I6C_reg14, &I6C_reg15,
    &I6C_reg16, &I6C_reg17,
    &I6C_reg18, &I6C_reg19,
    &I6C_reg20, &I6C_reg21,
    &I6C_reg22, &I6C_reg23,
    &I6C_reg24, &I6C_reg25,
    &I6C_reg26, &I6C_reg27,
    &I6C_reg28, &I6C_reg29,
    &I6C_reg30, &I6C_reg31,
    &I6C_reg32, &I6C_reg33,
    &I6C_reg34, &I6C_reg35,
    &I6C_reg36, &I6C_reg37,
    &I6C_reg38, &I6C_reg39,
    &I6C_reg40, &I6C_reg41,
    &I6C_reg42, &I6C_reg43,
    &I6C_reg44, &I6C_reg45,
    &I6C_reg46, &I6C_reg47,
    &I6C_reg48, &I6C_reg49,
    &I6C_reg50, &I6C_reg51,
    &I6C_reg52, &I6C_reg53,
    &I6C_reg54, &I6C_reg55,
    &I6C_reg56, &I6C_reg57,
    &I6C_reg58, &I6C_reg59,
    &I6C_reg60, &I6C_reg61,
    &I6C_reg62, &I6C_reg63,
    &I6C_reg64, &I6C_reg65,
    &I6C_reg66, &I6C_reg67,
    &I6C_reg68, &I6C_reg69,
    &I6C_reg70, &I6C_reg71,
    &I6C_reg72, &I6C_reg73,
    &I6C_reg74, &I6C_reg75,
    &I6C_reg76, &I6C_reg77,
    &I6C_reg78, &I6C_reg79,
    &I6C_reg80, &I6C_reg81,
    0,
};

MUXCTRL(I6E_reg0, 0x1F007E64, "PAD_PM_UART_RX1")
MUXCTRL(I6E_reg1, 0x1F007E80, "PAD_PM_UART_TX1")
MUXCTRL(I6E_reg2, 0x1F007E84, "PAD_PM_UART_RX")
MUXCTRL(I6E_reg3, 0x1F007E88, "PAD_PM_UART_TX")
MUXCTRL(I6E_reg4, 0x1F007E44, "PAD_PM_I2CM_SCL")
MUXCTRL(I6E_reg5, 0x1F007E48, "PAD_PM_I2CM_SDA")
MUXCTRL(I6E_reg6, 0x1F007E00, "PAD_PM_GPIO0")
MUXCTRL(I6E_reg7, 0x1F007E04, "PAD_PM_GPIO1")
MUXCTRL(I6E_reg8, 0x1F007E08, "PAD_PM_GPIO2")
MUXCTRL(I6E_reg9, 0x1F007E0C, "PAD_PM_GPIO3")
MUXCTRL(I6E_reg10, 0x1F007E10, "PAD_PM_GPIO4")
MUXCTRL(I6E_reg11, 0x1F007E14, "PAD_PM_GPIO5")
MUXCTRL(I6E_reg12, 0x1F007E18, "PAD_PM_GPIO6")
MUXCTRL(I6E_reg13, 0x1F007E1C, "PAD_PM_GPIO7")
MUXCTRL(I6E_reg14, 0x1F007E20, "PAD_PM_GPIO8")
MUXCTRL(I6E_reg15, 0x1F007E24, "PAD_PM_GPIO9")
MUXCTRL(I6E_reg16, 0x1F007E40, "PAD_PM_GPIO10")
MUXCTRL(I6E_reg17, 0x1F007E4C, "PAD_PM_SPI_CZ")
MUXCTRL(I6E_reg18, 0x1F007E50, "PAD_PM_SPI_CK")
MUXCTRL(I6E_reg19, 0x1F007E54, "PAD_PM_SPI_DI")
MUXCTRL(I6E_reg20, 0x1F007E58, "PAD_PM_SPI_DO")
MUXCTRL(I6E_reg21, 0x1F007E5C, "PAD_PM_SPI_WPZ")
MUXCTRL(I6E_reg22, 0x1F007E60, "PAD_PM_SPI_HLD")
MUXCTRL(I6E_reg23, 0x1F002848, "PAD_SAR_GPIO0")
MUXCTRL(I6E_reg24, 0x1F002848, "PAD_SAR_GPIO1")
MUXCTRL(I6E_reg25, 0x1F002848, "PAD_SAR_GPIO2")
MUXCTRL(I6E_reg26, 0x1F002848, "PAD_SAR_GPIO3")
MUXCTRL(I6E_reg27, 0x1F002848, "PAD_SAR_GPIO4")
MUXCTRL(I6E_reg28, 0x1F002848, "PAD_SAR_GPIO5")
MUXCTRL(I6E_reg29, 0x1F207840, "PAD_SD0_GPIO0")
MUXCTRL(I6E_reg30, 0x1F207844, "PAD_SD0_CDZ")
MUXCTRL(I6E_reg31, 0x1F207848, "PAD_SD0_D1")
MUXCTRL(I6E_reg32, 0x1F20784C, "PAD_SD0_D0")
MUXCTRL(I6E_reg33, 0x1F207850, "PAD_SD0_CLK")
MUXCTRL(I6E_reg34, 0x1F207854, "PAD_SD0_CMD")
MUXCTRL(I6E_reg35, 0x1F207858, "PAD_SD0_D3")
MUXCTRL(I6E_reg36, 0x1F20785C, "PAD_SD0_D2")
MUXCTRL(I6E_reg37, 0x1F207860, "PAD_I2S0_MCLK")
MUXCTRL(I6E_reg38, 0x1F207864, "PAD_I2S0_BCK")
MUXCTRL(I6E_reg39, 0x1F207868, "PAD_I2S0_WCK")
MUXCTRL(I6E_reg40, 0x1F20786C, "PAD_I2S0_DI")
MUXCTRL(I6E_reg41, 0x1F207870, "PAD_I2S0_DO")
MUXCTRL(I6E_reg42, 0x1F207874, "PAD_I2C0_SCL")
MUXCTRL(I6E_reg43, 0x1F207878, "PAD_I2C0_SDA")
MUXCTRL(I6E_reg44, 0x1F207880, "PAD_ETH_LED0")
MUXCTRL(I6E_reg45, 0x1F207884, "PAD_ETH_LED1")
MUXCTRL(I6E_reg46, 0x1F207888, "PAD_FUART_RX")
MUXCTRL(I6E_reg47, 0x1F20788C, "PAD_FUART_TX")
MUXCTRL(I6E_reg48, 0x1F207890, "PAD_FUART_CTS")
MUXCTRL(I6E_reg49, 0x1F207894, "PAD_FUART_RTS")
MUXCTRL(I6E_reg50, 0x1F207898, "PAD_SD1_CDZ")
MUXCTRL(I6E_reg51, 0x1F20789C, "PAD_SD1_D1")
MUXCTRL(I6E_reg52, 0x1F2078A0, "PAD_SD1_D0")
MUXCTRL(I6E_reg53, 0x1F2078A4, "PAD_SD1_CLK")
MUXCTRL(I6E_reg54, 0x1F2078A8, "PAD_SD1_CMD")
MUXCTRL(I6E_reg55, 0x1F2078AC, "PAD_SD1_D3")
MUXCTRL(I6E_reg56, 0x1F2078B0, "PAD_SD1_D2")
MUXCTRL(I6E_reg57, 0x1F2078B4, "PAD_SD1_GPIO0")
MUXCTRL(I6E_reg58, 0x1F2078B8, "PAD_SD1_GPIO1")
MUXCTRL(I6E_reg59, 0x1F207800, "PAD_GPIO0")
MUXCTRL(I6E_reg60, 0x1F207804, "PAD_GPIO1")
MUXCTRL(I6E_reg61, 0x1F207808, "PAD_GPIO2")
MUXCTRL(I6E_reg62, 0x1F20780C, "PAD_GPIO3")
MUXCTRL(I6E_reg63, 0x1F207810, "PAD_GPIO4")
MUXCTRL(I6E_reg64, 0x1F207814, "PAD_GPIO5")
MUXCTRL(I6E_reg65, 0x1F207818, "PAD_GPIO6")
MUXCTRL(I6E_reg66, 0x1F20781C, "PAD_GPIO7")
MUXCTRL(I6E_reg67, 0x1F2078D8, "PAD_SR0_IO00")
MUXCTRL(I6E_reg68, 0x1F2078DC, "PAD_SR0_IO01")
MUXCTRL(I6E_reg69, 0x1F2078E0, "PAD_SR0_IO02")
MUXCTRL(I6E_reg70, 0x1F2078E4, "PAD_SR0_IO03")
MUXCTRL(I6E_reg71, 0x1F2078E8, "PAD_SR0_IO04")
MUXCTRL(I6E_reg72, 0x1F2078EC, "PAD_SR0_IO05")
MUXCTRL(I6E_reg73, 0x1F2078F0, "PAD_SR0_IO06")
MUXCTRL(I6E_reg74, 0x1F2078F4, "PAD_SR0_IO07")
MUXCTRL(I6E_reg75, 0x1F2078F8, "PAD_SR0_IO08")
MUXCTRL(I6E_reg76, 0x1F2078FC, "PAD_SR0_IO09")
MUXCTRL(I6E_reg77, 0x1F207900, "PAD_SR0_IO10")
MUXCTRL(I6E_reg78, 0x1F207904, "PAD_SR0_IO11")
MUXCTRL(I6E_reg79, 0x1F207908, "PAD_SR0_IO12")
MUXCTRL(I6E_reg80, 0x1F20790C, "PAD_SR0_IO13")
MUXCTRL(I6E_reg81, 0x1F207910, "PAD_SR0_IO14")
MUXCTRL(I6E_reg82, 0x1F207914, "PAD_SR0_IO15")
MUXCTRL(I6E_reg83, 0x1F207918, "PAD_SR0_IO16")
MUXCTRL(I6E_reg84, 0x1F20791C, "PAD_SR0_IO17")
MUXCTRL(I6E_reg85, 0x1F207920, "PAD_SR0_IO18")
MUXCTRL(I6E_reg86, 0x1F207924, "PAD_SR0_IO19")
MUXCTRL(I6E_reg87, 0x1F207928, "PAD_SR1_IO00")
MUXCTRL(I6E_reg88, 0x1F20792C, "PAD_SR1_IO01")
MUXCTRL(I6E_reg89, 0x1F207930, "PAD_SR1_IO02")
MUXCTRL(I6E_reg90, 0x1F207934, "PAD_SR1_IO03")
MUXCTRL(I6E_reg91, 0x1F207938, "PAD_SR1_IO04")
MUXCTRL(I6E_reg92, 0x1F20793C, "PAD_SR1_IO05")
MUXCTRL(I6E_reg93, 0x1F207940, "PAD_SR1_IO06")
MUXCTRL(I6E_reg94, 0x1F207944, "PAD_SR1_IO07")
MUXCTRL(I6E_reg95, 0x1F207948, "PAD_SR1_IO08")
MUXCTRL(I6E_reg96, 0x1F20794C, "PAD_SR1_IO09")
MUXCTRL(I6E_reg97, 0x1F207950, "PAD_SR1_IO10")
MUXCTRL(I6E_reg98, 0x1F207954, "PAD_SR1_IO11")
MUXCTRL(I6E_reg99, 0x1F207958, "PAD_SR1_IO12")
MUXCTRL(I6E_reg100, 0x1F20795C, "PAD_SR1_IO13")
MUXCTRL(I6E_reg101, 0x1F207960, "PAD_SR1_IO14")
MUXCTRL(I6E_reg102, 0x1F207964, "PAD_SR1_IO15")
MUXCTRL(I6E_reg103, 0x1F207968, "PAD_SR1_IO16")
MUXCTRL(I6E_reg104, 0x1F20796C, "PAD_SR1_IO17")
MUXCTRL(I6E_reg105, 0x1F207970, "PAD_SR1_IO18")
MUXCTRL(I6E_reg106, 0x1F207974, "PAD_SR1_IO19")
MUXCTRL(I6E_reg107, 0x1F207820, "PAD_GPIO8")
MUXCTRL(I6E_reg108, 0x1F207824, "PAD_GPIO9")
MUXCTRL(I6E_reg109, 0x1F207828, "PAD_GPIO10")
MUXCTRL(I6E_reg110, 0x1F20782C, "PAD_GPIO11")
MUXCTRL(I6E_reg111, 0x1F207830, "PAD_GPIO12")
MUXCTRL(I6E_reg112, 0x1F207834, "PAD_GPIO13")
MUXCTRL(I6E_reg113, 0x1F207838, "PAD_GPIO14")
MUXCTRL(I6E_reg114, 0x1F20783C, "PAD_GPIO15")
MUXCTRL(I6E_reg115, 0x1F2078C0, "PAD_SPI_CZ")
MUXCTRL(I6E_reg116, 0x1F2078C4, "PAD_SPI_CK")
MUXCTRL(I6E_reg117, 0x1F2078C8, "PAD_SPI_DI")
MUXCTRL(I6E_reg118, 0x1F2078CC, "PAD_SPI_DO")
MUXCTRL(I6E_reg119, 0x1F2078D0, "PAD_SPI_WPZ")
MUXCTRL(I6E_reg120, 0x1F2078D4, "PAD_SPI_HLD")
MUXCTRL(I6E_reg121, 0x1F2A2DC8, "PAD_ETH_RN")
MUXCTRL(I6E_reg122, 0x1F2A2DC8, "PAD_ETH_RP")
MUXCTRL(I6E_reg123, 0x1F2A2DC8, "PAD_ETH_TN")
MUXCTRL(I6E_reg124, 0x1F2A2DC8, "PAD_ETH_TP")
MUXCTRL(I6E_reg125, 0x1F284214, "PAD_USB2_DM")
MUXCTRL(I6E_reg126, 0x1F284214, "PAD_USB2_DP")

static const muxctrl_reg_t *I6E_regs[] = {
    &I6E_reg0,   &I6E_reg1,
    &I6E_reg2,   &I6E_reg3,
    &I6E_reg4,   &I6E_reg5,
    &I6E_reg6,   &I6E_reg7,
    &I6E_reg8,   &I6E_reg9,
    &I6E_reg10,  &I6E_reg11,
    &I6E_reg12,  &I6E_reg13,
    &I6E_reg14,  &I6E_reg15,
    &I6E_reg16,  &I6E_reg17,
    &I6E_reg18,  &I6E_reg19,
    &I6E_reg20,  &I6E_reg21,
    &I6E_reg22,  &I6E_reg23,
    &I6E_reg24,  &I6E_reg25,
    &I6E_reg26,  &I6E_reg27,
    &I6E_reg28,  &I6E_reg29,
    &I6E_reg30,  &I6E_reg31,
    &I6E_reg32,  &I6E_reg33,
    &I6E_reg34,  &I6E_reg35,
    &I6E_reg36,  &I6E_reg37,
    &I6E_reg38,  &I6E_reg39,
    &I6E_reg40,  &I6E_reg41,
    &I6E_reg42,  &I6E_reg43,
    &I6E_reg44,  &I6E_reg45,
    &I6E_reg46,  &I6E_reg47,
    &I6E_reg48,  &I6E_reg49,
    &I6E_reg50,  &I6E_reg51,
    &I6E_reg52,  &I6E_reg53,
    &I6E_reg54,  &I6E_reg55,
    &I6E_reg56,  &I6E_reg57,
    &I6E_reg58,  &I6E_reg59,
    &I6E_reg60,  &I6E_reg61,
    &I6E_reg62,  &I6E_reg63,
    &I6E_reg64,  &I6E_reg65,
    &I6E_reg66,  &I6E_reg67,
    &I6E_reg68,  &I6E_reg69,
    &I6E_reg70,  &I6E_reg71,
    &I6E_reg72,  &I6E_reg73,
    &I6E_reg74,  &I6E_reg75,
    &I6E_reg76,  &I6E_reg77,
    &I6E_reg78,  &I6E_reg79,
    &I6E_reg80,  &I6E_reg81,
    &I6E_reg82,  &I6E_reg83,
    &I6E_reg84,  &I6E_reg85,
    &I6E_reg86,  &I6E_reg87,
    &I6E_reg88,  &I6E_reg89,
    &I6E_reg90,  &I6E_reg91,
    &I6E_reg92,  &I6E_reg93,
    &I6E_reg94,  &I6E_reg95,
    &I6E_reg96,  &I6E_reg97,
    &I6E_reg98,  &I6E_reg99,
    &I6E_reg100, &I6E_reg101,
    &I6E_reg102, &I6E_reg103,
    &I6E_reg104, &I6E_reg105,
    &I6E_reg106, &I6E_reg107,
    &I6E_reg108, &I6E_reg109,
    &I6E_reg110, &I6E_reg111,
    &I6E_reg112, &I6E_reg113,
    &I6E_reg114, &I6E_reg115,
    &I6E_reg116, &I6E_reg117,
    &I6E_reg118, &I6E_reg119,
    &I6E_reg120, &I6E_reg121,
    &I6E_reg122, &I6E_reg123,
    &I6E_reg124, &I6E_reg125,
    &I6E_reg126, 0,
};

#endif
