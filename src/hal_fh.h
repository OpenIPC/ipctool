#ifndef HAL_FH_H
#define HAL_FH_H

#include <stdbool.h>

#define VENDOR_FH "Fullhan"

#define PMU_REG_BASE 0xf0000000
#define REG_PMU_CHIP_ID (PMU_REG_BASE + 0x0000)
#define REG_PMU_IP_VER (PMU_REG_BASE + 0x0004)
#define REG_PMU_FW_VER (PMU_REG_BASE + 0x0008)
#define REG_PMU_SYS_CTRL (PMU_REG_BASE + 0x000c)

bool fh_detect_cpu(char *chip_name);
unsigned long fh_totalmem(unsigned long *media_mem);
void setup_hal_fh();

#endif /* HAL_FH_H */
