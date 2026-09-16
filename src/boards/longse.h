#ifndef LONGSE_H
#define LONGSE_H

#include <stdbool.h>

#include "cjson/cJSON.h"

bool is_longse_board();
bool gather_longse_board_info();
const char *longse_version_of(const char *sofvar);

#endif /* LONGSE_H */
