#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "chipid.h"
#include "tools.h"
#include "vendors/xm.h"

bool is_xm_board() {
    if (!access("/proc/xm/xminfo", 0)) {
        strcpy(board_manufacturer, "Xiongmai");
        return true;
    }
    return false;
}

static void detect_nor_chip() {
    char buf[100];

    int fd = open("/dev/mtd0", 0x1002);
    if (fd < 0) {
        return;
    }

    // XMMTD_GETFLASHNAME
    memset(buf, 0, sizeof buf);
    if (ioctl(fd, 0x40044DAAu, &buf) >= 0) {
        sprintf(nor_chip, "      name: \"%s\"\n", buf);
    }

    // XMMTD_GETFLASHID
    uint32_t flash_id;
    if (ioctl(fd, 0x40044DA9u, &flash_id) >= 0) {
        sprintf(nor_chip + strlen(nor_chip), "      id: 0x%06x\n", flash_id);
    }

    close(fd);
}

static bool detect_xm_product() {
    char buf[256];

    if (!get_regex_line_from_file("/usr/bin/ProductDefinition",
                                  "\"Hardware\" : \"(.+)\"", buf,
                                  sizeof(buf))) {
        return false;
    }
    strcpy(board_id, buf);
    return true;
}

static bool extract_cloud_id() {
    char buf[256];

    if (!get_regex_line_from_file("/mnt/mtd/Config/SerialNumber", "([0-9a-f]+)",
                                  buf, sizeof(buf))) {
        return false;
    }
    sprintf(board_specific + strlen(board_specific), "  cloudId: %s\n", buf);
    return true;
}

void gather_xm_board_info() {
    detect_xm_product();
    extract_cloud_id();
    detect_nor_chip();
}
