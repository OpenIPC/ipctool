#ifndef HAL_ANYKA_H
#define HAL_ANYKA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The chip-ID word sits at the foot of the system controller, which every
 * AK37/AK39 memory map puts at the same physical address. /proc/iomem does
 * not name it -- the audio driver claims the surrounding 64 KB as
 * "akpcm_AnalogCtrlRegs" -- so it has to be spelled out here. */
#define AK_PA_SYSCTRL 0x08000000
#define AK_REG_CHIP_ID (AK_PA_SYSCTRL + 0x00)
/* The AK3918AV130 keeps its ID at the far end of the same block
 * (mach-ak3918av130.c: AK_VA_SYSCTRL + 0x3FC). */
#define AK_REG_CHIP_ID_AV (AK_PA_SYSCTRL + 0x3FC)

/* chip_generation values. The ID word names a generation, never the exact
 * part: a vendor kernel compares it against the single constant picked by its
 * CONFIG_CPU_AK39xx and prints whichever name it was built for, so the same
 * 0x20160100 is announced as AK3916, AK3918 or AK3919 by three different
 * boards. The part number comes from the machine string instead. */
/* The first generation, Aimer39 (AK3918, AK3916, AK3910): its 0x20120100 is
 * shared with some AK3918EV200 boards, so the board string decides. */
#define AK39_EV1 0x3918E100
#define AK39_EV2 0x3918E200
#define AK39_EV3 0x3918E300
#define AK39_EV330 0x3939E330
#define AK39_EV300L 0x3918E30C
#define AK39_AV100 0x3918A100
#define AK39_AV130 0x3918A130
#define AK37_D 0x3737D000
#define AK37_E 0x3737E000

bool anyka_detect_cpu(char *chip_name);
void anyka_setup_hal();

/* The parsing, split out and given its inputs as arguments. Nobody on the
 * project has an Anyka camera, so anyka_test.c feeding these fixture files is
 * the only check this code gets; anyka_detect_cpu() always passes the real
 * /proc paths. */
bool anyka_machine_name(const char *cpuinfo, char *out, size_t outlen);
int anyka_generation(uint32_t chip_id);
bool anyka_compatible_part(const char *compat, size_t len, char *out,
                           size_t outlen);
bool anyka_is_aimer39_dt(const char *compat, size_t len);
void anyka_model_name(const char *machine, const char *compat,
                      size_t compat_len, int generation, char *out,
                      size_t outlen);
unsigned long anyka_media_mem(const char *cmdline, const char *iomem);
unsigned long anyka_dt_memsize(const char *reg_path);
bool anyka_dt_gpio(const char *pattern, int *pin, bool *active_low);
unsigned anyka_peri_pll_mhz(uint32_t pll1);

#endif /* HAL_ANYKA_H */
