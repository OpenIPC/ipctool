#ifndef NAND_LAYOUT_H
#define NAND_LAYOUT_H

/*
 * The pure half of `ipctool nand-migrate`: everything that decides, as
 * opposed to everything that touches a device. Kept apart so it can be tested
 * on a host (nand_layout_test.c).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * u-boot-xmedia's stock bootcmd of the first FIT NAND layout: a FIT in a UBI
 * volume of its own named `kernel` (NAND_FITVOL_BOOTCOMMAND, e19b798a). Only a
 * camera that still carries it byte for byte is migrated: it proves a FIT
 * NAND build of that U-Boot, and every one of those has CMD_UBIFS.
 */
#define NM_BOOTCMD_FITVOL                                                      \
    "ubi part ubi; ubi read ${baseaddr} kernel; "                              \
    "if itest.l *${baseaddr} == 0xedfe0dd0; "                                  \
    "then setenv rootargs root=ubi0:rootfs rootfstype=ubifs; "                 \
    "else setenv rootargs root=/dev/ubiblock0_1 ubi.block=0,1; fi; "           \
    "setenv setargs setenv bootargs ${bootargs}; run setargs; "                \
    "bootm ${baseaddr}; reset"

/*
 * The current one (CONFIG_BOOTCOMMAND, a138a692): the kernel is a file in the
 * UBIFS rootfs. Byte for byte what a fresh install saves, so a later U-Boot
 * recognises it as its own.
 */
#define NM_BOOTCMD_UBIFS                                                       \
    "ubi part ubi; ubifsmount ubi0:rootfs; "                                   \
    "setenv rootargs root=ubi0:rootfs rootfstype=ubifs; "                      \
    "setenv setargs setenv bootargs ${bootargs}; run setargs; "                \
    "if ubifsload ${baseaddr} /boot/fitImage; then bootm ${baseaddr}; fi; "    \
    "if ubifsload ${baseaddr} /boot/uImage; then bootm ${baseaddr}; fi; reset"

// zlib's CRC-32, which U-Boot's env and (uninverted) UBIFS both use.
uint32_t nm_crc32(uint32_t crc, const void *buf, size_t len);

/*
 * A U-Boot env at the start of buf (CRC, then key=value\0...\0\0). Returns
 * the env's length, from the sizes U-Boot builds use, or 0 when no CRC
 * matches.
 */
size_t nm_env_locate(const uint8_t *buf, size_t size);

// The value of exactly `key` (not a key it is a prefix of), or NULL.
const char *nm_env_get(const uint8_t *env, size_t len, const char *key);

/*
 * Set exactly `key` to `value` in place and recompute the CRC. False when the
 * result would not fit, in which case env is unchanged.
 */
bool nm_env_set(uint8_t *env, size_t len, const char *key, const char *value);

/* What the running camera looks like, gathered by the caller. */
typedef struct {
    bool has_kernel_vol, has_rootfs_vol, has_data_vol;
    const char *cmdline;
    const uint8_t *kernel_head; // the kernel volume's first bytes
    size_t kernel_head_len;
    const char *bootcmd, *bootargs;
} nm_camera_t;

typedef enum {
    NM_REFUSE,
    // the retired layout: write the rootfs, switch bootcmd, drop the volume
    NM_MIGRATE,
    // a migration that stopped after switching bootcmd: only the unused
    // kernel volume is left, and it can go with the system running
    NM_DROP_KERNEL_VOL,
} nm_plan_t;

/*
 * What can be done with this camera. On NM_REFUSE *why says why not, in one
 * sentence.
 */
nm_plan_t nm_check_camera(const nm_camera_t *cam, const char **why);

/*
 * Whether img is a UBIFS image built for leb_size-byte LEBs that holds a
 * kernel at /boot/fitImage or /boot/uImage. NULL when it is, otherwise why
 * not. *kernel_name says which kernel it found.
 */
const char *nm_check_image(const uint8_t *img, size_t size, uint32_t leb_size,
                           const char **kernel_name);

#endif /* NAND_LAYOUT_H */
