#ifndef OPENWRT_H
#define OPENWRT_H

#include <stdbool.h>

#include "cjson/cJSON.h"

bool gather_openwrt_board_info();
bool is_openwrt_board();

#endif /* OPENWRT_H */
