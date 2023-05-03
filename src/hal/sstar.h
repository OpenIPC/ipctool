#ifndef HAL_SSTAR_H
#define HAL_SSTAR_H

#define CMD_PATH "/proc/cmdline"
#define TEMP_PATH "/sys/class/mstar/msys/TEMP_R"

#define BROM_ADDR1 0x3FE0
#define BROM_ADDR2 0x7FE0
#define CHIP_ADDR1 0x1F203150
#define CHIP_ADDR2 0x1F004058
#define MSTAR_ADDR 0x1F2025A4
#define SSTAR_ADDR 0x1F003C00

#define INFINITY3  0xC2
#define INFINITY5  0xED
#define MERCURY5   0xEE
#define INFINITY6  0xEF
#define INFINITY2M 0xF0
#define INFINITY6E 0xF1
#define INFINITY6B 0xF2
#define PIONEER3   0xF5

bool mstar_detect_cpu(char *chip_name);
bool sstar_detect_cpu(char *chip_name);
unsigned long sstar_totalmem(unsigned long *media_mem);
void sstar_setup_hal();

#endif /* HAL_SSTAR_H */
