#include "hal/tegra.h"

#include "hal/common.h"
#include "tools.h"

#include <ctype.h>
#include <string.h>

// /sys/bus/i2c/drivers/imx219/6-0010/name -> i2c-6 and 0x10
static unsigned char sony_addrs[] = {0x10, 0};
sensor_addr_t tegra_possible_i2c_addrs[] = {{SENSOR_SONY, sony_addrs},
                                            {0, NULL}};

bool tegra_detect_cpu(char *chip_name) {
    char buf[256];

    if (!dts_items_by_regex("/proc/device-tree/compatible",
                            "nvidia,(tegra[0-9-]+)", buf, sizeof(buf)))
        return false;

    strcpy(chip_name, buf);
    chip_name[0] = toupper(chip_name[0]);

    return true;
}

static int tegra_open_sensor_fd() {
    /* Currently works only when pipeline is active (ex for IMX219):
     * $ sudo i2ctransfer -f -y 6 w2@0x10 0x00 0x00 r1
     * 0x02
     * $ sudo i2ctransfer -f -y 6 w2@0x10 0x00 0x01 r1
     * 0x19
     */
    return universal_open_sensor_fd("/dev/i2c-6");
}

static int i2c_change_plain_addr(int fd, unsigned char addr) {
    if (ioctl(fd, I2C_SLAVE_FORCE, addr) < 0) {
        return -1;
    }
    return 0;
}

#ifndef STANDALONE_LIBRARY
static void chip_properties(cJSON *j_inner) {
    char buf[256];

    if (!line_from_file("/proc/device-tree/model", "NVIDIA (.+)", buf,
                        sizeof(buf)))
        return;

    ADD_PARAM("board", buf);
}

static void firmware_props(cJSON *j_inner) {
    char buf[1024];

    if (line_from_file("/sys/class/tegra-firmware/versions",
                       "Firmware timestamp: (.+)", buf, sizeof(buf))) {
        ADD_PARAM("tegra-firmware", buf);
    }
}
#endif

void tegra_setup_hal() {
    open_i2c_sensor_fd = tegra_open_sensor_fd;
    possible_i2c_addrs = tegra_possible_i2c_addrs;
    i2c_change_addr = i2c_change_plain_addr;
#ifndef STANDALONE_LIBRARY
    hal_chip_properties = chip_properties;
    hal_firmware_props = firmware_props;
#endif
}
