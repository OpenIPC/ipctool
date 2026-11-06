#include "hal/ambarella.h"

#include <string.h>

#include "hal/common.h"
#include "tools.h"

/* Defaults of the `addr` module parameter across the S3L SDK's VIN drivers,
 * the 8-bit form ipctool's tables use. AR0521 is the one part left out: it
 * answers on 0x6C, and the onsemi probe runs before the OmniVision one, so
 * listing it would have that probe reading an OV4689. */
static unsigned char sony_addrs[] = {0x34, 0};
static unsigned char onsemi_addrs[] = {0x20, 0};
static unsigned char omni_addrs[] = {0x6c, 0};

static sensor_addr_t ambarella_possible_i2c_addrs[] = {
    {SENSOR_SONY, sony_addrs},
    {SENSOR_ONSEMI, onsemi_addrs},
    {SENSOR_OMNIVISION, omni_addrs},
    {0, NULL}};

/* Every DT_MACHINE_START in the vendor tree reads "Ambarella <soc> (Flattened
 * Device Tree)". These kernels register no UART with /proc/iomem, and S3L
 * and S3LM share one machine string: the exact part (33M, 55M, 66, ...) is
 * known only to the DSP firmware once it runs, so "S3L" is as far as this
 * goes. */
bool ambarella_machine_name(const char *cpuinfo, char *out, size_t outlen) {
    if (!outlen)
        return false;

    if (!line_from_file(cpuinfo, "Hardware.+: Ambarella ([A-Za-z0-9]+)", out,
                        outlen))
        return false;

    out[outlen - 1] = '\0';
    return true;
}

bool ambarella_detect_cpu(char *chip_name) {
    char machine[128];

    if (!ambarella_machine_name("/proc/cpuinfo", machine, sizeof(machine)))
        return false;
    strcpy(chip_name, machine);

    /* chip_generation stays 0 on purpose. It keys the pad-mux, clock and boot
     * ROM tables, and ipctool has none for Ambarella, so 0 sends those paths to
     * their no-table answer and getchipfamily() back to the name. */
    return true;
}

void ambarella_setup_hal() {
    possible_i2c_addrs = ambarella_possible_i2c_addrs;
}
