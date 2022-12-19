#include <stdint.h>

#include "cjson/cJSON.h"
#include "cjson/cYAML.h"

#include "common.h"
#include "reginfo.h"
#include "tools.h"

#include "vendors/anjoy.h"
#include "vendors/buildroot.h"
#include "vendors/hankvision.h"
#include "vendors/openwrt.h"
#include "vendors/ruision.h"
#include "vendors/sstar.h"
#include "vendors/xm.h"

typedef struct {
    bool (*detector_fn)(void);
    bool (*gatherinfo_fn)(cJSON *);
} board_vendors_t;

static bool gather_anjoy_board(cJSON *j_inner) {
    gather_sstar_board_info(j_inner);
    return gather_anjoy_board_info();
}

static const board_vendors_t vendors[] = {
    {is_xm_board, gather_xm_board_info},
    {is_openwrt_board, gather_openwrt_board_info},
    {is_br_board, gather_br_board_info},
    {is_ruision_board, gather_ruision_board_info},
    {is_hankvision_board, gather_hankvision_board_info},
    {is_anjoy_board, gather_anjoy_board},
    {is_sstar_board, gather_sstar_board_info},
};

cJSON *detect_board() {
    cJSON *fake_root = cJSON_CreateObject();
    cJSON *j_inner = cJSON_CreateObject();
    cJSON_AddItemToObject(fake_root, "board", j_inner);

    for (size_t i = 0; i < ARRCNT(vendors); i++) {
        if (vendors[i].detector_fn())
            if (vendors[i].gatherinfo_fn(j_inner))
                break;
    }

    char buf[1024] = {0};
    const char *ircuts = gpio_possible_ircut(buf, sizeof(buf));
    if (ircuts) {
        ADD_PARAM("possible-IR-cut-GPIO", ircuts);
    }

    return fake_root;
}
