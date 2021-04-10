#include <stdbool.h>
#include <string.h>

#include <unistd.h>

#include "openipc.h"

#include "chipid.h"

bool is_openipc_board() {
    if (!access("/etc/openipc_donaters", 0)) {
        strcpy(board_manufacturer, "OpenIPC");
        return true;
    }
    return false;
}
