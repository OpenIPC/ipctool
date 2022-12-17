#include <stdio.h>
#include <string.h>

#include "chipid.h"
#include "hal_common.h"
#include "ram.h"
#include "tools.h"

cJSON *detect_ram() {
    cJSON *fake_root = cJSON_CreateObject();
    cJSON *j_inner = cJSON_CreateObject();
    cJSON_AddItemToObject(fake_root, "ram", j_inner);

    unsigned long media_mem = 0;
    uint32_t total_mem = hal_totalmem(&media_mem);
    ADD_PARAM_FMT("total", "%uM", rounded_num(total_mem / 1024));

    if (media_mem)
        ADD_PARAM_FMT("media", "%luM", media_mem / 1024);

    return fake_root;
}
