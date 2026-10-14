/* jffs2_test: the image `ipctool upgrade` carries a camera's settings in.
 *
 * Builds a tree like an OpenIPC overlay, makes an image of it, and reads the
 * image back node by node: every CRC the kernel checks, every name, and every
 * file's bytes reassembled from its pages. With an output path it also writes
 * the image there, for mtd-utils' jffs2reader or a kernel to have the final
 * word. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include "jffs2.h"

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

#define ERASE 0x10000

static uint32_t get16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t get32(const uint8_t *p) {
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t jcrc(const void *data, size_t len) {
    uint32_t crc = 0;
    const uint8_t *p = data;
    while (len--) {
        crc ^= *p++;
        for (int j = 0; j < 8; j++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320 : crc >> 1;
    }
    return crc;
}

/* What the image says, rebuilt: names by (parent, name), and file contents
 * by inode. */
#define MAXI 64
static struct {
    uint32_t pino, ino;
    char name[256];
} dents[MAXI];
static int ndents;
static struct {
    uint32_t mode, isize;
    uint8_t data[20000];
    int seen;
} inodes[MAXI];

static void parse(const uint8_t *img, size_t len) {
    for (size_t blk = 0; blk < len; blk += ERASE) {
        size_t off = blk;
        while (off + 12 <= blk + ERASE) {
            const uint8_t *p = img + off;
            if (get16(p) == 0xffff) {
                // Erased to the end of the block
                for (size_t i = off; i < blk + ERASE; i++)
                    CHECK(img[i] == 0xff, "data after free space at %#zx", i);
                break;
            }
            CHECK(get16(p) == 0x1985, "bad magic at %#zx", off);
            if (get16(p) != 0x1985)
                return;
            uint32_t type = get16(p + 2), totlen = get32(p + 4);
            CHECK(get32(p + 8) == jcrc(p, 8), "header crc at %#zx", off);
            CHECK(off + totlen <= blk + ERASE, "node crosses a block at %#zx",
                  off);
            if (off == blk)
                CHECK(type == 0x2003 && totlen == 12,
                      "block %#zx starts without a cleanmarker", blk);
            if (type == 0xe001) {
                CHECK(get32(p + 32) == jcrc(p, 32), "dirent crc at %#zx", off);
                uint8_t n = p[28];
                CHECK(get32(p + 36) == jcrc(p + 40, n), "name crc at %#zx",
                      off);
                if (ndents < MAXI) {
                    dents[ndents].pino = get32(p + 12);
                    dents[ndents].ino = get32(p + 20);
                    memcpy(dents[ndents].name, p + 40, n);
                    dents[ndents].name[n] = 0;
                    ndents++;
                }
            } else if (type == 0xe002) {
                CHECK(get32(p + 64) == jcrc(p, 60), "inode crc at %#zx", off);
                uint32_t ino = get32(p + 12), doff = get32(p + 44),
                         dlen = get32(p + 48);
                CHECK(get32(p + 60) == jcrc(p + 68, dlen), "data crc at %#zx",
                      off);
                CHECK(p[56] == 0 && get32(p + 52) == dlen,
                      "compressed or sized oddly at %#zx", off);
                if (ino < MAXI && doff + dlen <= sizeof(inodes[0].data)) {
                    inodes[ino].mode = get32(p + 20);
                    inodes[ino].isize = get32(p + 28);
                    memcpy(inodes[ino].data + doff, p + 68, dlen);
                    inodes[ino].seen = 1;
                }
            } else
                CHECK(type == 0x2003 || type == 0x2004,
                      "unknown node %#x at %#zx", type, off);
            off += (totlen + 3) & ~3u;
        }
    }
}

static uint32_t lookup(uint32_t pino, const char *name) {
    for (int i = 0; i < ndents; i++)
        if (dents[i].pino == pino && !strcmp(dents[i].name, name))
            return dents[i].ino;
    return 0;
}

static void writefile(const char *path, const void *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(data, 1, len, f);
        fclose(f);
    }
}

int main(int argc, char **argv) {
    char root[] = "/tmp/jffs2_test.XXXXXX";
    if (!mkdtemp(root)) {
        perror("mkdtemp");
        return 1;
    }
    char path[512];
    // The shape of an OpenIPC overlay: root/ with the settings, work/ that
    // overlayfs keeps for itself
    snprintf(path, sizeof(path), "%s/root", root);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/root/etc", root);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/work", root);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/work/work", root);
    mkdir(path, 0700);
    snprintf(path, sizeof(path), "%s/work/work/leftover", root);
    writefile(path, "x", 1);

    // Bigger than a page, so it is several nodes
    static uint8_t big[10000];
    for (size_t i = 0; i < sizeof(big); i++)
        big[i] = i * 7;
    snprintf(path, sizeof(path), "%s/root/etc/majestic.yaml", root);
    writefile(path, big, sizeof(big));
    snprintf(path, sizeof(path), "%s/root/etc/shadow", root);
    writefile(path, "root:$1$x:1:0:99999:7:::\n", 25);
    chmod(path, 0600);
    snprintf(path, sizeof(path), "%s/root/etc/custom.ok", root);
    writefile(path, "", 0);
    snprintf(path, sizeof(path), "%s/root/etc/default.ini", root);
    if (symlink("imx335.ini", path))
        perror("symlink");
    // An overlayfs whiteout, where the test may make one
    snprintf(path, sizeof(path), "%s/root/etc/inittab.removed", root);
    bool whiteout = !mknod(path, S_IFCHR | 0, makedev(0, 0));

    jffs2_img_t *img = jffs2_new(ERASE, 0x2b0000);
    CHECK(jffs2_add_tree(img, root, "work"), "add_tree: %s",
          jffs2_error(img));
    size_t len;
    const uint8_t *data = jffs2_data(img, &len);
    CHECK(len && len % ERASE == 0, "image of %zu bytes", len);
    parse(data, len);

    uint32_t r = lookup(1, "root"), w = lookup(1, "work");
    CHECK(r && w, "root/ or work/ missing");
    // overlayfs makes work/work again as it mounts
    CHECK(!lookup(w, "work"), "work/ should be carried empty");
    uint32_t etc = lookup(r, "etc");
    uint32_t y = lookup(etc, "majestic.yaml");
    CHECK(y && inodes[y].isize == sizeof(big) &&
              !memcmp(inodes[y].data, big, sizeof(big)),
          "majestic.yaml did not come back whole");
    uint32_t s = lookup(etc, "shadow");
    CHECK(s && (inodes[s].mode & 07777) == 0600 && inodes[s].isize == 25 &&
              !memcmp(inodes[s].data, "root:$1$x", 9),
          "shadow lost its bytes or its mode");
    uint32_t c = lookup(etc, "custom.ok");
    CHECK(c && inodes[c].seen && inodes[c].isize == 0, "empty file missing");
    uint32_t l = lookup(etc, "default.ini");
    CHECK(l && S_ISLNK(inodes[l].mode) && inodes[l].isize == 10 &&
              !memcmp(inodes[l].data, "imx335.ini", 10),
          "symlink wrong");
    if (whiteout) {
        uint32_t wo = lookup(etc, "inittab.removed");
        CHECK(wo && S_ISCHR(inodes[wo].mode) && inodes[wo].isize == 2 &&
                  get16(inodes[wo].data) == 0,
              "whiteout lost");
    } else
        printf("(no whiteout: mknod needs root here)\n");
    CHECK(inodes[1].seen && S_ISDIR(inodes[1].mode), "no root inode");

    // A limit it cannot keep is refused, not truncated
    jffs2_img_t *tiny = jffs2_new(ERASE, ERASE / 8);
    CHECK(!jffs2_add_tree(tiny, root, "work") && *jffs2_error(tiny),
          "an image past its limit was accepted");
    jffs2_free(tiny);

    if (argc > 1)
        writefile(argv[1], data, len);
    jffs2_free(img);

    char cmd[600];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", root);
    if (system(cmd))
        fprintf(stderr, "could not remove %s\n", root);

    if (!failed)
        puts("jffs2_test: OK");
    return failed;
}
