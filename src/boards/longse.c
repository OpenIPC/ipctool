#include <string.h>
#include <unistd.h>

#include "longse.h"

#include "chipid.h"
#include "tools.h"

#define DEVICE_ID "/var/cfg/Ver.ini"

bool is_longse_board() {
    if (!access(DEVICE_ID, 0)) {
        return true;
    }
    return false;
}

/* Ver.ini carries one interesting line:
 *
 *   [VERSION]
 *   sofvar=3516CV500_IMX307_B1T1A1M0C0P1_W_9.1.53.1
 *   sofvar=RV1109_IMX335NAND_BVH0L0A0T0Q0_W_21.1.36.6
 *
 * SoC and sensor lead, then an undecoded per-model feature code, then `_W_`
 * and the firmware version. Only the version is pulled out by name -- the SoC
 * and the sensor ipctool has already detected for itself, and guessing at the
 * feature code would be inventing meaning. The whole string is reported as
 * `firmware-id` so the part we do not parse is still there to read. */
bool gather_longse_board_info(cJSON *j_inner) {
    char buf[256];

    if (!line_from_file(DEVICE_ID, "sofvar=(.+)", buf, sizeof(buf))) {
        return false;
    }
    ADD_PARAM("vendor", "Longse");
    ADD_PARAM("firmware-id", buf);

    const char *ver = strstr(buf, "_W_");
    if (ver && *(ver + 3)) {
        ADD_PARAM("version", ver + 3);
    }
    return true;
}
