#include "ethernet.h"

#include "cjson/cYAML.h"
#include "hal/common.h"
#include "network.h"
#include "tools.h"

cJSON *detect_ethernet() {
    cJSON *j_inner = cJSON_CreateObject();

    char mac[20];
    if (get_mac_address(mac, sizeof mac)) {
        ADD_PARAM("mac", mac);
    };

    if (hal_detect_ethernet)
        hal_detect_ethernet(j_inner);

    return j_inner;
}
