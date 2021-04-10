#include <string.h>
#include <unistd.h>

#include "hankvision.h"

#include "chipid.h"
#include "tools.h"

#define DEVICE_ID "/mnt/flash/productinfo/deviceid.txt"

bool is_hankvision_board() {
    if (!access(DEVICE_ID, 0)) {
        strcpy(board_manufacturer, "Hankvision");
        return true;
    }
    return false;
}

bool gather_hankvision_board_info() {
    char buf[256];

    if (!get_regex_line_from_file(DEVICE_ID, "DEVICEID (.+)", buf,
                                  sizeof(buf))) {
        return false;
    }
    strcpy(board_id, buf);
    return true;
}
