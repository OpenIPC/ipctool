#ifndef HAL_FH_H
#define HAL_FH_H

#include <stdbool.h>

#define PMU_REG_BASE 0xf0000000
#define REG_PMU_CHIP_ID (PMU_REG_BASE + 0x0000)
#define REG_PMU_IP_VER (PMU_REG_BASE + 0x0004)
#define REG_PMU_FW_VER (PMU_REG_BASE + 0x0008)
#define REG_PMU_SYS_CTRL (PMU_REG_BASE + 0x000c)
#define REG_PMU_SPC_IO_STATUS (PMU_REG_BASE + 0x0054)
#define REG_PMU_CHIP_INFO (PMU_REG_BASE + 0x033c)

/* chip_generation for the parts hal/fh_padmux.c has a pad table for, and the
 * two it knows it has none for. "FH", the model, and a last nibble for the
 * revision: 1 V100, 2 V200, 3 V210, 4 V201. */
enum {
    CHIP_FH8852V100 = 0x46488521,
    CHIP_FH8856V100 = 0x46488561,
    CHIP_FH8852V200 = 0x46488522,
    CHIP_FH8856V200 = 0x46488562,
    CHIP_FH8858V200 = 0x46488582,
    CHIP_FH8852V210 = 0x46488523,
    CHIP_FH8856V210 = 0x46488563,
    CHIP_FH8858V210 = 0x46488583,
    CHIP_FH8856V201 = 0x46488564,
    CHIP_FH8858V201 = 0x46488584,
};

bool fh_detect_cpu(char *chip_name);
unsigned long fh_totalmem(unsigned long *media_mem);
void fh_setup_hal();

#endif /* HAL_FH_H */
