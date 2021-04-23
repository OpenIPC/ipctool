#include "buildroot.h"
#include "openipc.h"

#include <string.h>

#include "chipid.h"
#include "tools.h"

/*
# cat /etc/os-release
NAME=Buildroot
VERSION=2020.02-gb7e4b9a-dirty
ID=buildroot
VERSION_ID=2020.02
PRETTY_NAME="Buildroot 2020.02"
*/

bool is_br_board() {
    char buf[256];

    if (get_regex_line_from_file("/etc/os-release", "NAME=(.+)", buf,
                                 sizeof(buf))) {
        if (!strcmp(buf, "Buildroot"))
            return true;
        return false;
    }
    return false;
}

static bool detect_br_product() {
    char buf[256];
    if (get_regex_line_from_file("/etc/os-release", "VERSION_ID=(.+)", buf,
                                 sizeof(buf))) {
        strcpy(board_ver, buf);
        return true;
    }
    return false;
}

void gather_br_board_info() {
    detect_br_product();
    is_openipc_board();
}
