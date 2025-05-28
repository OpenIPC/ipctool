#ifndef HAL_NOVATEK_H
#define HAL_NOVATEK_H

#include <stdbool.h>

bool novatek_detect_cpu(char *chip_name);
unsigned long novatek_totalmem(unsigned long *media_mem);
void novatek_setup_hal();

#define TOP_VERSION_REG_OFS 0xF0

/* na51000, na51089, na51068, na51000, na51090, na51055 */
#define IOADDR_GLOBAL_BASE (0xF0000000)
/* na51090 */
//#define IOADDR_GLOBAL_BASE (0x2F0000000)

/* na51000, na51089, na51000, na51090, na51090, na51055 */
#define IOADDR_TOP_REG_BASE (IOADDR_GLOBAL_BASE + 0x00010000)
/* na51068 */
//#define IOADDR_TOP_REG_BASE (IOADDR_GLOBAL_BASE + 0x0E030000)

enum CHIP_ID {
    CHIP_NA51055 = 0x4821, // NT98525, 128Kb L2, 5M@30
                           // NT98528, 256Kb L2, 4K@30
    CHIP_NA51084 = 0x5021,
    CHIP_NA51089 = 0x7021, // NT98562, 64Mb internal RAM
                           // NT98566, 128Mb internal RAM
    CHIP_NA51090 = 0xBC21,
    CHIP_NA51103 = 0x8B20 // NVT98332G
};

#endif /* HAL_NOVATEK_H */
