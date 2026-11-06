#ifndef HAL_AMBARELLA_H
#define HAL_AMBARELLA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool ambarella_machine_name(const char *cpuinfo, char *out, size_t outlen);
bool ambarella_detect_cpu(char *chip_name);
void ambarella_setup_hal();

/* The DSP carve-out, as a physical start and a size in bytes. It runs to the
 * top of DRAM, which is the only place total RAM can be read from here: the
 * kernel is capped well below it by mem= on the command line. */
bool ambarella_iavmem_from_dt(const char *path, uint32_t *start,
                              uint32_t *size);
bool ambarella_iavmem_from_log(const char *log, uint32_t *start,
                               uint32_t *size);
unsigned long ambarella_media_kb(const char *iomem, uint32_t start,
                                 uint32_t size);
bool ambarella_family(const char *iav, char *out, size_t outlen);

#endif /* HAL_AMBARELLA_H */
