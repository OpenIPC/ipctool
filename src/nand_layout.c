#include "nand_layout.h"

#include <stdlib.h>
#include <string.h>

uint32_t nm_crc32(uint32_t crc, const void *buf, size_t len) {
    static uint32_t table[256];
    if (!table[1]) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = c & 1 ? 0xEDB88320 ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    }
    const uint8_t *p = buf;
    crc = ~crc;
    while (len--)
        crc = table[(crc ^ *p++) & 0xff] ^ (crc >> 8);
    return ~crc;
}

static uint32_t le32(const uint8_t *p) {
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t le64(const uint8_t *p) {
    return le32(p) | (uint64_t)le32(p + 4) << 32;
}

size_t nm_env_locate(const uint8_t *buf, size_t size) {
    static const size_t lens[] = {0x10000, 0x20000, 0x40000, 0x8000,
                                  0x4000,  0x2000,  0x1000};
    for (size_t i = 0; i < sizeof(lens) / sizeof(lens[0]); i++) {
        if (lens[i] > size)
            continue;
        if (nm_crc32(0, buf + 4, lens[i] - 4) == le32(buf))
            return lens[i];
    }
    return 0;
}

const char *nm_env_get(const uint8_t *env, size_t len, const char *key) {
    size_t klen = strlen(key);
    const char *p = (const char *)env + 4, *end = (const char *)env + len;
    while (p < end && *p) {
        size_t n = strnlen(p, end - p);
        // A CRC only proves the bytes are what was saved, not that they
        // are a well-formed env: an entry must end inside it
        if (n == (size_t)(end - p))
            return NULL;
        if (n > klen && !strncmp(p, key, klen) && p[klen] == '=')
            return p + klen + 1;
        p += n + 1;
    }
    return NULL;
}

bool nm_env_set(uint8_t *env, size_t len, const char *key, const char *value) {
    size_t klen = strlen(key), vlen = strlen(value);
    uint8_t *out = calloc(len, 1);
    if (!out)
        return false;
    size_t o = 4;
    const char *p = (const char *)env + 4, *end = (const char *)env + len;
    while (p < end && *p) {
        size_t n = strnlen(p, end - p);
        if (n == (size_t)(end - p))
            goto toolong;
        if (!(n > klen && !strncmp(p, key, klen) && p[klen] == '=')) {
            // +2: this entry's NUL and the list's
            if (o + n + 2 > len)
                goto toolong;
            memcpy(out + o, p, n);
            o += n + 1;
        }
        p += n + 1;
    }
    if (o + klen + 1 + vlen + 2 > len)
        goto toolong;
    memcpy(out + o, key, klen);
    out[o + klen] = '=';
    memcpy(out + o + klen + 1, value, vlen);
    uint32_t crc = nm_crc32(0, out + 4, len - 4);
    out[0] = crc;
    out[1] = crc >> 8;
    out[2] = crc >> 16;
    out[3] = crc >> 24;
    memcpy(env, out, len);
    free(out);
    return true;
toolong: // or not a well-formed env to begin with
    free(out);
    return false;
}

static bool has_word(const char *s, const char *w) {
    size_t n = strlen(w);
    for (const char *p = s; (p = strstr(p, w)); p += n)
        if ((p == s || p[-1] == ' ') &&
            (p[n] == '\0' || p[n] == ' ' || p[n] == '\n'))
            return true;
    return false;
}

#define REFUSE(msg)                                                            \
    do {                                                                       \
        *why = msg;                                                            \
        return NM_REFUSE;                                                      \
    } while (0)

nm_plan_t nm_check_camera(const nm_camera_t *cam, const char **why) {
    *why = NULL;
    if (!cam->has_rootfs_vol || !cam->has_data_vol)
        REFUSE("no UBI volumes named rootfs and rootfs_data: not a UBI NAND "
               "camera");
    if (!cam->cmdline || !has_word(cam->cmdline, "root=ubi0:rootfs"))
        REFUSE("the kernel was not booted with root=ubi0:rootfs: not the "
               "UBIFS layout this migrates from");
    if (!cam->has_kernel_vol)
        REFUSE("there is no kernel volume: this camera is already on the "
               "current layout, and sysupgrade upgrades it");
    if (!cam->bootcmd)
        REFUSE("the U-Boot environment has no bootcmd");
    if (!strcmp(cam->bootcmd, NM_BOOTCMD_UBIFS)) {
        // Booted with this bootcmd, so the running rootfs is where U-Boot
        // found the kernel; the caller still checks /boot is there.
        return NM_DROP_KERNEL_VOL;
    }
    if (strcmp(cam->bootcmd, NM_BOOTCMD_FITVOL))
        REFUSE("bootcmd is not the stock one of the kernel-volume layout; an "
               "edited bootcmd is left to its owner");
    // A FIT starts with the FDT magic; a legacy uImage with 0x27051956
    if (cam->kernel_head_len < 4 || le32(cam->kernel_head) != 0xedfe0dd0)
        REFUSE("the kernel volume does not hold a FIT, so this U-Boot is not "
               "known to boot one from UBIFS");
    // An argument of its own: "x=${rootargs}y" would expand to no root=
    if (!cam->bootargs || !has_word(cam->bootargs, "${rootargs}"))
        REFUSE("bootargs does not take its root from ${rootargs}, which the "
               "new bootcmd sets");
    return NM_MIGRATE;
}

#define UBIFS_MAGIC 0x06101831
#define UBIFS_CH_SZ 24
#define UBIFS_DENT_NODE 2
#define UBIFS_SB_NODE 6
#define UBIFS_ITYPE_REG 0
#define UBIFS_ITYPE_DIR 1
#define UBIFS_ROOT_INO 1

// A node header at p with room for it in the image and a CRC that holds.
static uint32_t ubifs_node(const uint8_t *p, size_t room) {
    if (room < UBIFS_CH_SZ || le32(p) != UBIFS_MAGIC)
        return 0;
    uint32_t len = le32(p + 16);
    if (len < UBIFS_CH_SZ || len > room)
        return 0;
    // UBIFS starts from ~0 and does not invert at the end
    if (~nm_crc32(0, p + 8, len - 8) != le32(p + 4))
        return 0;
    return len;
}

/*
 * The entry for `name` in directory `parent` as the filesystem has it now:
 * of every directory-entry node naming it, the one written last (highest
 * sequence number). An entry that was deleted is a later node with inode 0;
 * then, as when there is no entry at all, this returns 0. Otherwise the
 * entry's inode, with its type in *type.
 */
static uint64_t ubifs_lookup(const uint8_t *img, size_t size, uint32_t leb,
                             uint64_t parent, const char *name, uint8_t *type) {
    size_t nlen_want = strlen(name);
    uint64_t best_sqnum = 0, best_inum = 0;
    for (size_t off = 0; off + UBIFS_CH_SZ <= size; off += 8) {
        const uint8_t *p = img + off;
        if (p[20] != UBIFS_DENT_NODE)
            continue;
        // A node never crosses the end of its LEB
        size_t room = leb - off % leb;
        if (room > size - off)
            room = size - off;
        uint32_t len = ubifs_node(p, room);
        if (len < 56)
            continue;
        uint64_t sqnum = le64(p + 8);
        uint16_t nlen = p[50] | p[51] << 8;
        if (le32(p + 24) == parent && nlen == nlen_want && 56u + nlen <= len &&
            !memcmp(p + 56, name, nlen) && sqnum >= best_sqnum) {
            best_sqnum = sqnum;
            best_inum = le64(p + 40);
            *type = p[49];
        }
        off += (len + 7) / 8 * 8 - 8;
    }
    return best_inum;
}

const char *nm_check_image(const uint8_t *img, size_t size, uint32_t leb_size,
                           const char **kernel_name) {
    *kernel_name = NULL;
    uint32_t len = ubifs_node(img, size < leb_size ? size : leb_size);
    if (!len || img[20] != UBIFS_SB_NODE || len < 40)
        return "not a UBIFS image (no superblock at its start)";
    if (le32(img + 36) != leb_size)
        return "built for a different LEB size than this flash's";

    /*
     * Directory entries carry their names uncompressed, so finding /boot and
     * the kernel in it needs no UBIFS reader: an entry's key starts with
     * the inode of the directory it is in. Nodes are 8-byte aligned within
     * a LEB, and LEBs are back to back.
     */
    uint8_t type = 0;
    uint64_t boot =
        ubifs_lookup(img, size, leb_size, UBIFS_ROOT_INO, "boot", &type);
    if (!boot || type != UBIFS_ITYPE_DIR)
        return "there is no /boot in it, so U-Boot would find no kernel";
    static const char *const kernels[] = {"fitImage", "uImage"};
    for (size_t i = 0; i < sizeof kernels / sizeof *kernels; i++)
        if (ubifs_lookup(img, size, leb_size, boot, kernels[i], &type) &&
            type == UBIFS_ITYPE_REG) {
            *kernel_name = i ? "/boot/uImage" : "/boot/fitImage";
            return NULL;
        }
    return "/boot holds neither fitImage nor uImage, so U-Boot would find "
           "no kernel";
}
