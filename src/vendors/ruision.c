#include <string.h>
#include <unistd.h>

#include "ruision.h"

#include "chipid.h"
#include "tools.h"

#define DEVICE_ID "/custom/info/custominfo.ini"

bool is_ruision_board() {
    if (!access(DEVICE_ID, 0)) {
        strcpy(board_manufacturer, "Ruision");
        return true;
    }
    return false;
}

bool gather_ruision_board_info() {
    char buf[256];

    if (!get_regex_line_from_file(DEVICE_ID, "DeviceModel=(.+)", buf,
                                  sizeof(buf))) {
        return false;
    }
    strcpy(board_id, buf);
    return true;
}
