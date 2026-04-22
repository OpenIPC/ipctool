#ifndef MTD_H
#define MTD_H

#include "cjson/cJSON.h"
#include <mtd/mtd-abi.h>

typedef bool (*cb_mtd)(int i, const char *name, struct mtd_info_user *mtd,
                       void *ctx);

cJSON *get_mtd_info();
char *open_mtdblock(int i, int *fd, uint32_t size, int flags);
void enum_mtd_info(void *ctx, cb_mtd cb);
bool mtd_write(int mtd, uint32_t offset, uint32_t erasesize, const char *data,
               size_t size);
int mtd_unlock_cmd();
int mtd_erase_block(int fd, int offset, int erasesize);

#define MAX_UBI_VOLS 8

typedef struct {
    int vol_id;
    char name[64];
    long long data_bytes;
} ubi_vol_info_t;

int find_ubi_for_mtd(int mtd_num);
int enum_ubi_volumes(int ubi_num, ubi_vol_info_t *vols, int max_vols);
char *read_ubi_volume(int ubi_num, int vol_id, size_t data_bytes,
                      size_t *out_len);

#endif /* MTD_H */
