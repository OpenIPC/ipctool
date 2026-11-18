#include "hal/axera.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "chipid.h"
#include "hal/common.h"
#include "tools.h"

static unsigned char sony_addrs[] = {0x34, 0};
static unsigned char ssens_addrs[] = {0x60, 0};
static unsigned char omni_addrs[] = {0x6c, 0};

static sensor_addr_t axera_possible_i2c_addrs[] = {
    {SENSOR_SONY, sony_addrs},
    {SENSOR_SMARTSENS, ssens_addrs},
    {SENSOR_OMNIVISION, omni_addrs},
    {0, NULL}};

/*
 * Axera has no readable chip-ID register documented anywhere public; the
 * kernel's root compatible names the die. AX620U's own devicetree (real
 * deployed firmware) says "axera,ax620x", AX620E/AX630C/AX620Q's say
 * "axera,ax620e", so map the AX620U spelling to the family OpenIPC builds.
 */
bool axera_detect_cpu(char *chip_name) {
    char buf[64];

    if (!line_from_file("/proc/device-tree/compatible", "axera,(ax[0-9a-z]+)",
                        buf, sizeof(buf)))
        return false;
    if (!strcmp(buf, "ax620x"))
        strcpy(buf, "ax620u");
    strcpy(chip_name, buf);
    return true;
}

/*
 * The sensor bus as the boards' own devicetrees and sensor libraries use it:
 * AX620U's on i2c8 (OS04A10 at 0x36 on the JFtech A8 board), AX620E's on
 * i2c6 (SC450AI at 0x30 on the Xiongmai IVG-A6, i2c@4856000).
 */
static int axera_open_i2c_fd() {
    char adapter_name[FILENAME_MAX];

    i2c_adapter_nr = strcmp(chip_name, "ax620u") ? 6 : 8;
    snprintf(adapter_name, sizeof(adapter_name), "/dev/i2c-%d", i2c_adapter_nr);
    return universal_open_sensor_fd(adapter_name);
}

void axera_setup_hal() {
    possible_i2c_addrs = axera_possible_i2c_addrs;
    open_i2c_sensor_fd = axera_open_i2c_fd;
}
