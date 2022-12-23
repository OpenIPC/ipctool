#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "chipid.h"
#include "tools.h"
#include "vendors/openwrt.h"

bool is_openwrt_board() {
    if (!access("/etc/openwrt_version", 0)) {
        return true;
    }
    return false;
}

static bool detect_openwrt_product(cJSON *j_inner) {
    char buf[256];

    if (!line_from_file("/etc/openwrt_version", "(.+)", buf, sizeof(buf))) {
        return false;
    }
    ADD_PARAM("vendor", "OpenWrt");
    ADD_PARAM("version", buf);
    return true;
}

bool gather_openwrt_board_info(cJSON *j_inner) {
    return detect_openwrt_product(j_inner);
}
