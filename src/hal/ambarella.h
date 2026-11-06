#ifndef HAL_AMBARELLA_H
#define HAL_AMBARELLA_H

#include <stdbool.h>
#include <stddef.h>

bool ambarella_machine_name(const char *cpuinfo, char *out, size_t outlen);
bool ambarella_detect_cpu(char *chip_name);
void ambarella_setup_hal();

#endif /* HAL_AMBARELLA_H */
