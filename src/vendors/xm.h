#ifndef XM_H
#define XM_H

#include <stdbool.h>

void gather_xm_board_info();
bool is_xm_board();
bool xm_spiflash_unlock_and_erase(int fd, uint32_t offset, uint32_t size);
bool xm_flash_init(int fd);

#endif /* XM_H */
