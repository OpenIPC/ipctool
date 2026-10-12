/*
 * nand_layout_test: what `ipctool nand-migrate` decides before it touches
 * anything. Synthetic env and UBIFS buffers always; with NM_ENV=<env dump>
 * and NM_IMAGE=<rootfs.ubifs> it also reads a real camera's env partition
 * and a real image (LEB size 126976, 2 KiB pages on 128 KiB blocks).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nand_layout.h"

static int failed;
#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);               \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, "\n");                                             \
            failed = 1;                                                        \
        }                                                                      \
    } while (0)

static void put32(uint8_t *p, uint32_t v) {
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}

static void make_env(uint8_t *env, size_t len, const char *const *kv) {
    memset(env, 0, len);
    size_t o = 4;
    for (; *kv; kv++) {
        strcpy((char *)env + o, *kv);
        o += strlen(*kv) + 1;
    }
    put32(env, nm_crc32(0, env + 4, len - 4));
}

static void test_env(void) {
    enum { LEN = 0x40000 };
    uint8_t *env = malloc(LEN);
    const char *kv[] = {"bootcmdx=decoy", "bootcmd=" NM_BOOTCMD_FITVOL,
                        "bootargs=mem=${osmem} ${rootargs}", "soc=gk7205v510",
                        NULL};
    make_env(env, LEN, kv);

    CHECK(nm_env_locate(env, LEN) == LEN, "a 256K env not found");
    CHECK(nm_env_locate(env, LEN - 1) == 0, "env found in too short a buffer");
    // A key that is a prefix of another is not that key
    const char *v = nm_env_get(env, LEN, "bootcmd");
    CHECK(v && !strcmp(v, NM_BOOTCMD_FITVOL), "bootcmd read as '%s'", v);
    CHECK(!nm_env_get(env, LEN, "boot"), "a key prefix matched");

    CHECK(nm_env_set(env, LEN, "bootcmd", NM_BOOTCMD_UBIFS), "set failed");
    CHECK(nm_env_locate(env, LEN) == LEN, "CRC not recomputed");
    v = nm_env_get(env, LEN, "bootcmd");
    CHECK(v && !strcmp(v, NM_BOOTCMD_UBIFS), "bootcmd not replaced");
    v = nm_env_get(env, LEN, "bootcmdx");
    CHECK(v && !strcmp(v, "decoy"), "the decoy was touched");
    v = nm_env_get(env, LEN, "soc");
    CHECK(v && !strcmp(v, "gk7205v510"), "another key lost");

    // Too long to fit: refused, env untouched
    uint8_t small[64];
    const char *skv[] = {"a=1", NULL};
    make_env(small, sizeof(small), skv);
    uint8_t copy[64];
    memcpy(copy, small, sizeof(small));
    CHECK(!nm_env_set(small, sizeof(small), "bootcmd", NM_BOOTCMD_UBIFS),
          "an overlong value accepted");
    CHECK(!memcmp(copy, small, sizeof(small)), "a refused set changed env");
    free(env);
}

static void test_camera(void) {
    static const uint8_t fit[] = {0xd0, 0x0d, 0xfe, 0xed};
    static const uint8_t uimage[] = {0x27, 0x05, 0x19, 0x56};
    const char *cmdline = "mem=128M console=ttyAMA0,115200 panic=20 init=/init "
                          "root=ubi0:rootfs rootfstype=ubifs ubi.mtd=2,2048\n";
    nm_camera_t good = {
        .has_kernel_vol = true,
        .has_rootfs_vol = true,
        .has_data_vol = true,
        .cmdline = cmdline,
        .kernel_head = fit,
        .kernel_head_len = 4,
        .bootcmd = NM_BOOTCMD_FITVOL,
        .bootargs = "mem=128M console=ttyAMA0,115200 ${rootargs} ${mtdparts}",
    };
    const char *why;
#define PLAN(c) nm_check_camera(&(c), &why)
    nm_camera_t c = good;
    CHECK(PLAN(c) == NM_MIGRATE, "the kvol camera refused: %s", why);

    c = good;
    c.has_kernel_vol = false;
    c.bootcmd = NM_BOOTCMD_UBIFS;
    CHECK(PLAN(c) == NM_REFUSE && why, "a current-layout camera accepted");

    c = good;
    c.bootcmd = NM_BOOTCMD_UBIFS;
    CHECK(PLAN(c) == NM_DROP_KERNEL_VOL,
          "a half-migrated camera not finished: %s", why);

    c = good;
    c.kernel_head = uimage;
    CHECK(PLAN(c) == NM_REFUSE && why, "a uImage kernel volume accepted");

    c = good;
    c.bootcmd = NM_BOOTCMD_FITVOL "; echo mine";
    CHECK(PLAN(c) == NM_REFUSE && why, "an edited bootcmd accepted");

    c = good;
    c.bootargs = "mem=128M root=ubi0:rootfs rootfstype=ubifs";
    CHECK(PLAN(c) == NM_REFUSE && why, "bootargs without ${rootargs} accepted");

    c = good;
    c.cmdline = "root=/dev/ubiblock0_1 ubi.block=0,1";
    CHECK(PLAN(c) == NM_REFUSE && why, "a ubiblock camera accepted");

    c = good;
    c.cmdline = "root=ubi0:rootfs_data";
    CHECK(PLAN(c) == NM_REFUSE && why,
          "root=ubi0:rootfs_data taken for rootfs");

    c = good;
    c.has_data_vol = false;
    CHECK(PLAN(c) == NM_REFUSE && why, "no rootfs_data accepted");
}

/* A UBIFS node: common header + body, CRC'd as UBIFS does. */
static size_t node(uint8_t *p, uint8_t type, const uint8_t *body, size_t blen) {
    size_t len = 24 + blen;
    memset(p, 0, 24);
    put32(p, 0x06101831);
    put32(p + 16, len);
    p[20] = type;
    memcpy(p + 24, body, blen);
    put32(p + 4, ~nm_crc32(0, p + 8, len - 8));
    return (len + 7) / 8 * 8;
}

static size_t dent(uint8_t *p, uint32_t parent, uint64_t inum, uint8_t itype,
                   const char *name) {
    uint8_t b[64] = {0};
    size_t nlen = strlen(name);
    put32(b, parent);
    put32(b + 4, 0x12345 | 2u << 29);
    put32(b + 16, inum);
    put32(b + 20, inum >> 32);
    b[25] = itype;
    b[26] = nlen;
    memcpy(b + 32, name, nlen + 1);
    return node(p, 2, b, 32 + nlen + 1);
}

static size_t sb(uint8_t *p, uint32_t leb) {
    uint8_t b[64] = {0};
    put32(b + 12, leb);
    return node(p, 6, b, sizeof(b));
}

static void test_image(void) {
    enum { LEB = 126976 };
    uint8_t *img = calloc(3, LEB);
    const char *k;

    sb(img, LEB);
    size_t o = LEB;
    o += dent(img + o, 1, 7, 1, "boot");
    o += dent(img + o, 1, 8, 1, "etc");
    o += dent(img + o, 7, 9, 0, "fitImage");
    CHECK(!nm_check_image(img, 3 * LEB, LEB, &k) && k &&
              !strcmp(k, "/boot/fitImage"),
          "a /boot/fitImage image refused: %s",
          nm_check_image(img, 3 * LEB, LEB, &k));
    CHECK(nm_check_image(img, 3 * LEB, 129024, &k), "LEB mismatch accepted");

    // The kernel in another directory is not the kernel U-Boot loads
    memset(img + LEB, 0, 2 * LEB);
    o = LEB;
    o += dent(img + o, 1, 7, 1, "boot");
    o += dent(img + o, 8, 9, 0, "fitImage");
    CHECK(nm_check_image(img, 3 * LEB, LEB, &k), "a stray fitImage accepted");

    // A deleted /boot entry is no /boot
    memset(img + LEB, 0, 2 * LEB);
    o = LEB;
    o += dent(img + o, 1, 0, 1, "boot");
    CHECK(nm_check_image(img, 3 * LEB, LEB, &k), "a deleted /boot accepted");

    // A uImage-only image boots too
    memset(img + LEB, 0, 2 * LEB);
    o = LEB;
    o += dent(img + o, 7, 9, 0, "uImage"); // before its directory, as in a log
    o += dent(img + o, 1, 7, 1, "boot");
    CHECK(!nm_check_image(img, 3 * LEB, LEB, &k) && k &&
              !strcmp(k, "/boot/uImage"),
          "a /boot/uImage image refused");

    // A corrupted entry is not believed
    img[LEB + 60] ^= 1;
    CHECK(nm_check_image(img, 3 * LEB, LEB, &k), "a bad CRC believed");

    // Not UBIFS at all (a squashfs)
    memset(img, 0, LEB);
    memcpy(img, "hsqs", 4);
    CHECK(nm_check_image(img, 3 * LEB, LEB, &k), "a squashfs accepted");
    free(img);
}

static uint8_t *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    rewind(f);
    uint8_t *b = malloc(*len);
    if (fread(b, 1, *len, f) != *len) {
        free(b);
        b = NULL;
    }
    fclose(f);
    return b;
}

static void test_real(void) {
    const char *p = getenv("NM_ENV");
    size_t len;
    uint8_t *b;
    if (p && (b = slurp(p, &len))) {
        size_t el = nm_env_locate(b, len);
        CHECK(el, "no env in %s", p);
        const char *v = el ? nm_env_get(b, el, "bootcmd") : NULL;
        CHECK(v && !strcmp(v, NM_BOOTCMD_FITVOL),
              "%s: bootcmd is not the stock kvol one: '%s'", p, v);
        printf("real env: %zu bytes, bootcmd is the kvol stock one\n", el);
        free(b);
    }
    p = getenv("NM_IMAGE");
    if (p && (b = slurp(p, &len))) {
        const char *k, *why = nm_check_image(b, len, 126976, &k);
        CHECK(!why, "%s refused: %s", p, why);
        if (!why)
            printf("real image: %zu bytes, kernel %s\n", len, k);
        free(b);
    }
}

int main(void) {
    test_env();
    test_camera();
    test_image();
    test_real();
    if (!failed)
        printf("nand_layout_test: ok\n");
    return failed;
}
