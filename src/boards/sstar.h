#ifndef SSTAR_H
#define SSTAR_H

#include <stdbool.h>

#include "cjson/cJSON.h"

bool is_sstar_board();
bool gather_sstar_board_info(cJSON *j_inner);

#endif
