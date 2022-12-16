#include <string.h>
#include <unistd.h>

#include "hankvision.h"

#include "chipid.h"
#include "tools.h"

#define DEVICE_ID "/mnt/flash/productinfo/deviceid.txt"

bool is_hankvision_board() {
    if (!access(DEVICE_ID, 0)) {
        return true;
    }
    return false;
}

bool gather_hankvision_board_info(cJSON *j_inner) {
    char buf[256];

    if (!get_regex_line_from_file(DEVICE_ID, "DEVICEID (.+)", buf,
                                  sizeof(buf))) {
        return false;
    }
    ADD_PARAM("vendor", "Hankvision");
    ADD_PARAM("model", buf);
    return true;
}
