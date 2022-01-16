#include "cjson/cYAML.h"
#include "ethernet.h"
#include "hal_common.h"
#include "network.h"
#include "tools.h"

cJSON *detect_ethernet() {
    cJSON *fake_root = cJSON_CreateObject();
    cJSON *j_inner = cJSON_CreateObject();
    cJSON_AddItemToObject(fake_root, "ethernet", j_inner);

    char mac[20];
    if (get_mac_address(mac, sizeof mac)) {
        ADD_PARAM("mac", mac);
    };

    if (hal_detect_ethernet)
        hal_detect_ethernet(j_inner);

    return fake_root;
}
