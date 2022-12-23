#include <string.h>
#include <unistd.h>

#include "ruision.h"

#include "chipid.h"
#include "tools.h"

#define DEVICE_ID "/custom/info/custominfo.ini"

bool is_ruision_board() {
    if (!access(DEVICE_ID, 0)) {
        return true;
    }
    return false;
}

bool gather_ruision_board_info(cJSON *j_inner) {
    char buf[256];

    if (!line_from_file(DEVICE_ID, "DeviceModel=(.+)", buf, sizeof(buf))) {
        return false;
    }
    ADD_PARAM("vendor", "Ruision");
    ADD_PARAM("model", buf);
    return true;
}
