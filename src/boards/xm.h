#ifndef XM_H
#define XM_H

#include <stdbool.h>

#include "cjson/cJSON.h"

bool gather_xm_board_info(cJSON *j_inner);
bool is_xm_board();
bool xm_spiflash_unlock_and_erase(int fd, uint32_t offset, uint32_t size);
bool xm_flash_init(int fd);
bool xm_kill_stuff(bool force);

#endif /* XM_H */
