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

// UBI ioctl definitions — inlined to avoid broken <mtd/ubi-user.h> in old
// musl toolchains where __packed is not defined as __attribute__((packed)).
#include <sys/ioctl.h>

#ifndef UBI_IOC_MAGIC
#define UBI_IOC_MAGIC 'o'
#define UBI_CTRL_IOC_MAGIC 'o'
#define UBI_VOL_IOC_MAGIC 'O'

#define UBI_DEV_NUM_AUTO (-1)
#define UBI_MAX_VOLUME_NAME 127
#define UBI_DYNAMIC_VOLUME 3

struct ubi_attach_req {
    int32_t ubi_num;
    int32_t mtd_num;
    int32_t vid_hdr_offset;
    int16_t max_beb_per1024;
    int8_t disable_fm;
    int8_t need_resv_pool;
    int8_t padding[8];
};

struct ubi_mkvol_req {
    int32_t vol_id;
    int32_t alignment;
    int64_t bytes;
    int8_t vol_type;
    uint8_t flags;
    int16_t name_len;
    int8_t padding2[4];
    char name[UBI_MAX_VOLUME_NAME + 1];
} __attribute__((packed));

#define UBI_IOCMKVOL _IOW(UBI_IOC_MAGIC, 0, struct ubi_mkvol_req)
#define UBI_IOCATT _IOW(UBI_CTRL_IOC_MAGIC, 64, struct ubi_attach_req)
#define UBI_IOCDET _IOW(UBI_CTRL_IOC_MAGIC, 65, int32_t)
#define UBI_IOCVOLUP _IOW(UBI_VOL_IOC_MAGIC, 0, int64_t)
#endif

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
