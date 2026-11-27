#ifndef HAL_SSTAR_H
#define HAL_SSTAR_H

#include <stdbool.h>
#include <stddef.h>

#define CMD_PATH "/proc/cmdline"
#define TEMP_PATH "/sys/class/mstar/msys/TEMP_R"

#define BROM_ADDR1 0x3FE0
#define BROM_ADDR2 0x7FE0
#define CHIP_ADDR1 0x1F203150
#define CHIP_ADDR2 0x1F004058
#define MSTAR_ADDR 0x1F2025A4
#define SSTAR_ADDR 0x1F003C00
/* CHIPTOP's bond strap (bank 0x101E, reg 0x48): the package, from which the
 * IPL picks its DDR setting and prints it after "D-" in its first lines. */
#define SSTAR_BOND_ADDR 0x1F203D20

#define INFINITY3  0xC2 // Infinity3
#define INFINITY5  0xED // Pretzel
#define MERCURY5   0xEE // Mercury5
#define INFINITY6  0xEF // Macaron
#define INFINITY2M 0xF0 // Taiyaki
#define INFINITY6E 0xF1 // Pudding
#define INFINITY6B 0xF2 // Ispahan
#define MERCURY6   0xF4 // Tiramisu
#define PIONEER3   0xF5 // Ikayaki
#define INFINITY6C 0xF9 // Maruko
#define INFINITY6F 0xFB // Souffle

/* Infinity2M packages by their bond strap (its low byte, as the IPL reads
 * it). 0x1F also has a DDR setting in both IPLs, but no part is known by it. */
#define I2M_BOND_SSR621Q 0x1B
#define I2M_BOND_SSD201  0x1D
#define I2M_BOND_SSD202D 0x1E

bool mstar_detect_cpu(char *chip_name);
bool sstar_detect_cpu(char *chip_name);
void sstar_setup_hal();
bool sstar_get_die_id(char *buf, size_t len);
const char *getchipfamily();

#endif /* HAL_SSTAR_H */
