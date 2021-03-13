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
        strcpy(board_manufacturer, "OpenWrt");
        return true;
    }
    return false;
}

static bool detect_openwrt_product() {
    char buf[256];

    if (!get_regex_line_from_file("/etc/openwrt_version", "(.+)", buf,
                                  sizeof(buf))) {
        return false;
    }
    strcpy(board_ver, buf);
    return true;
}

void gather_openwrt_board_info() { detect_openwrt_product(); }
