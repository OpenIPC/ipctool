#include "hal/bcm.h"

#include "hal/common.h"
#include "tools.h"

#include <string.h>

#ifndef STANDALONE_LIBRARY
#include "cjson/cJSON.h"
#endif

static unsigned char omni_addrs[] = {0x36, 0};

static sensor_addr_t my_possible_i2c_addrs[] = {{SENSOR_OMNIVISION, omni_addrs},
                                                {0, NULL}};

bool bcm_detect_cpu(char *chip_name) {
    char buf[256];

    if (!line_from_file("/proc/cpuinfo", "Hardware.+: (BCM[0-9-]+)", buf,
                        sizeof(buf)))
        return false;

    strcpy(chip_name, buf);

    return true;
}

#ifndef STANDALONE_LIBRARY
static void cpuinfo_param(cJSON *j_inner, char *name) {
    char out[256], pattern[256];

    snprintf(pattern, sizeof(pattern), "%s.+: (.+)", name);
    if (!line_from_file("/proc/cpuinfo", pattern, out, sizeof(out)))
        return;

    lsnprintf(pattern, sizeof(pattern), name);
    ADD_PARAM(pattern, out);
}

static void chip_properties(cJSON *j_inner) {
    cpuinfo_param(j_inner, "Revision");
    cpuinfo_param(j_inner, "Serial");
    cpuinfo_param(j_inner, "Model");
}
#endif

static int i2c_change_plain_addr(int fd, unsigned char addr) {
    if (ioctl(fd, I2C_SLAVE_FORCE, addr) < 0) {
        return -1;
    }
    return 0;
}

/* For using I2C functions make sure you have:
 * dtparam=i2c_arm=on
 * dtparam=i2c0=on
 *  in your /boot/config.txt
 */
void bcm_setup_hal() {
    possible_i2c_addrs = my_possible_i2c_addrs;
    i2c_change_addr = i2c_change_plain_addr;
#ifndef STANDALONE_LIBRARY
    hal_chip_properties = chip_properties;
#endif
}
