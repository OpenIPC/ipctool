#ifndef MTD_H
#define MTD_H

struct mtd_info_user {
    uint8_t type;
    uint32_t flags;
    uint32_t size; // Total size of the MTD
    uint32_t erasesize;
    uint32_t oobblock; // Size of OOB blocks (e.g. 512)
    uint32_t oobsize;  // Amount of OOB data per block (e.g. 16)
    uint32_t ecctype;
    uint32_t eccsize;
};

typedef void (*cb_mtd)(int i, const char *name, struct mtd_info_user *mtd,
                       void *ctx);

void print_mtd_info();
void enum_mtd_info(void *ctx, cb_mtd cb);

#endif /* MTD_H */
