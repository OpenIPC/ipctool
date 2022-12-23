#ifndef BUILDROOT_H
#define BUILDROOT_H

#include <stdbool.h>

#include "cjson/cJSON.h"

bool is_br_board();
bool is_openipc_board();
bool gather_br_board_info(cJSON *element);

#endif /* BUILDROOT_H */
