#include "jffs2.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <unistd.h>

#define JFFS2_MAGIC 0x1985
#define NODE_DIRENT 0xe001
#define NODE_INODE 0xe002
#define NODE_CLEANMARKER 0x2003
#define NODE_PADDING 0x2004
#define COMPR_NONE 0x00
#define HDR_LEN 12
#define INODE_LEN 68
#define DIRENT_LEN 40
#define PAGE 4096

struct jffs2_img {
    uint8_t *buf;
    size_t len, cap, limit, erasesize;
    uint32_t next_ino, version;
    int entries, xattrs;
    char err[160];
};

/* The kernel's crc32(0, ...): reflected 0xEDB88320, no inversion in or out,
 * which is not what zlib's crc32 computes. */
static uint32_t jcrc(const void *data, size_t len) {
    static uint32_t table[256];
    if (!table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t r = i;
            for (int j = 0; j < 8; j++)
                r = (r & 1) ? (r >> 1) ^ 0xEDB88320 : r >> 1;
            table[i] = r;
        }
    uint32_t crc = 0;
    const uint8_t *p = data;
    while (len--)
        crc = table[(crc ^ *p++) & 0xff] ^ (crc >> 8);
    return crc;
}

static void put16(uint8_t *p, uint16_t v) {
    p[0] = v;
    p[1] = v >> 8;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}

static bool fail(jffs2_img_t *img, const char *fmt, ...) {
    if (!*img->err) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(img->err, sizeof(img->err), fmt, ap);
        va_end(ap);
    }
    return false;
}

static bool reserve(jffs2_img_t *img, size_t more) {
    if (img->len + more > img->limit)
        return fail(img, "the settings need more than the %zu KB there is "
                         "room for",
                    img->limit / 1024);
    if (img->len + more <= img->cap)
        return true;
    size_t cap = img->cap ? img->cap : img->erasesize;
    while (cap < img->len + more)
        cap *= 2;
    uint8_t *b = realloc(img->buf, cap);
    if (!b)
        return fail(img, "out of memory building the settings image");
    memset(b + img->cap, 0xff, cap - img->cap);
    img->buf = b;
    img->cap = cap;
    return true;
}

static void header(uint8_t *p, uint16_t type, uint32_t totlen) {
    put16(p, JFFS2_MAGIC);
    put16(p + 2, type);
    put32(p + 4, totlen);
    put32(p + 8, jcrc(p, 8));
}

/* Room for a node of `len` bytes in the current erase block: the rest of a
 * block that cannot take it becomes a padding node, and every block starts
 * with a cleanmarker. */
static bool place(jffs2_img_t *img, size_t len) {
    size_t in_block = img->len % img->erasesize;
    if (in_block && in_block + len > img->erasesize) {
        size_t rest = img->erasesize - in_block;
        if (!reserve(img, rest))
            return false;
        if (rest >= HDR_LEN)
            header(img->buf + img->len, NODE_PADDING, rest);
        img->len += rest;
        in_block = 0;
    }
    if (!in_block) {
        if (!reserve(img, HDR_LEN))
            return false;
        header(img->buf + img->len, NODE_CLEANMARKER, HDR_LEN);
        img->len += HDR_LEN;
    }
    return reserve(img, (len + 3) & ~3u);
}

static bool emit_inode(jffs2_img_t *img, uint32_t ino, const struct stat *st,
                       uint32_t isize, uint32_t offset, const void *data,
                       uint32_t dlen) {
    if (!place(img, INODE_LEN + dlen))
        return false;
    uint8_t *p = img->buf + img->len;
    header(p, NODE_INODE, INODE_LEN + dlen);
    put32(p + 12, ino);
    put32(p + 16, ++img->version);
    put32(p + 20, st->st_mode);
    put16(p + 24, st->st_uid);
    put16(p + 26, st->st_gid);
    put32(p + 28, isize);
    put32(p + 32, st->st_mtime); // atime: the camera does not keep it
    put32(p + 36, st->st_mtime);
    put32(p + 40, st->st_ctime);
    put32(p + 44, offset);
    put32(p + 48, dlen); // csize
    put32(p + 52, dlen); // dsize
    p[56] = COMPR_NONE;
    p[57] = COMPR_NONE;
    put16(p + 58, 0);
    if (dlen)
        memcpy(p + INODE_LEN, data, dlen);
    put32(p + 60, jcrc(p + INODE_LEN, dlen));
    put32(p + 64, jcrc(p, INODE_LEN - 8));
    img->len += (INODE_LEN + dlen + 3) & ~3u;
    return true;
}

static uint8_t dirent_type(mode_t m) {
    // DT_* values, as the kernel's jffs2 stores them
    return S_ISDIR(m)    ? 4
           : S_ISREG(m)  ? 8
           : S_ISLNK(m)  ? 10
           : S_ISCHR(m)  ? 2
           : S_ISBLK(m)  ? 6
           : S_ISFIFO(m) ? 1
           : S_ISSOCK(m) ? 12
                         : 0;
}

static bool emit_dirent(jffs2_img_t *img, uint32_t pino, uint32_t ino,
                        const char *name, mode_t mode, time_t mtime) {
    size_t n = strlen(name);
    if (n > 254)
        return fail(img, "'%s' is too long a name for jffs2", name);
    if (!place(img, DIRENT_LEN + n))
        return false;
    uint8_t *p = img->buf + img->len;
    header(p, NODE_DIRENT, DIRENT_LEN + n);
    put32(p + 12, pino);
    put32(p + 16, ++img->version);
    put32(p + 20, ino);
    put32(p + 24, mtime);
    p[28] = n;
    p[29] = dirent_type(mode);
    p[30] = p[31] = 0;
    put32(p + 32, jcrc(p, DIRENT_LEN - 8));
    put32(p + 36, jcrc(name, n));
    memcpy(p + DIRENT_LEN, name, n);
    img->len += (DIRENT_LEN + n + 3) & ~3u;
    return true;
}

static bool add_file(jffs2_img_t *img, uint32_t ino, const char *path,
                     const struct stat *st) {
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return fail(img, "cannot read %s: %s", path, strerror(errno));
    uint8_t page[PAGE];
    uint32_t off = 0;
    bool ok = true;
    for (;;) {
        ssize_t n = read(fd, page, sizeof(page));
        if (n < 0) {
            ok = fail(img, "cannot read %s: %s", path, strerror(errno));
            break;
        }
        if (!n)
            break;
        if (!(ok = emit_inode(img, ino, st, st->st_size, off, page, n)))
            break;
        off += n;
    }
    close(fd);
    // An empty file is still an inode
    if (ok && !off)
        ok = emit_inode(img, ino, st, 0, 0, NULL, 0);
    return ok;
}

static int count_xattrs(const char *path) {
    ssize_t n = llistxattr(path, NULL, 0);
    if (n <= 0)
        return 0;
    char *names = malloc(n);
    int count = 0;
    if (names && (n = llistxattr(path, names, n)) > 0)
        for (char *p = names; p < names + n; p += strlen(p) + 1)
            count++;
    free(names);
    return count;
}

static bool add_dir(jffs2_img_t *img, uint32_t ino, const char *path,
                    const char *rel, const char *skip) {
    DIR *d = opendir(path);
    if (!d)
        return fail(img, "cannot read %s: %s", path, strerror(errno));
    bool ok = true;
    struct dirent *e;
    while (ok && (e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        char child[4096], crel[4096];
        snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
        snprintf(crel, sizeof(crel), "%s%s%s", rel, *rel ? "/" : "",
                 e->d_name);
        struct stat st;
        if (lstat(child, &st)) {
            ok = fail(img, "cannot stat %s: %s", child, strerror(errno));
            break;
        }
        uint32_t cino = img->next_ino++;
        img->entries++;
        img->xattrs += count_xattrs(child);
        if (!(ok = emit_dirent(img, ino, cino, e->d_name, st.st_mode,
                               st.st_mtime)))
            break;
        if (S_ISDIR(st.st_mode)) {
            ok = emit_inode(img, cino, &st, 0, 0, NULL, 0);
            if (ok && !(skip && !strcmp(crel, skip)))
                ok = add_dir(img, cino, child, crel, skip);
        } else if (S_ISREG(st.st_mode)) {
            ok = add_file(img, cino, child, &st);
        } else if (S_ISLNK(st.st_mode)) {
            char target[4096];
            ssize_t n = readlink(child, target, sizeof(target));
            if (n < 0)
                ok = fail(img, "cannot read link %s: %s", child,
                          strerror(errno));
            else
                ok = emit_inode(img, cino, &st, n, 0, target, n);
        } else if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) {
            // The kernel's old 16-bit encoding where it fits, which is what
            // it reads back by length; overlayfs's whiteout is 0:0
            unsigned maj = major(st.st_rdev), min = minor(st.st_rdev);
            uint8_t dev[4];
            if (maj < 256 && min < 256) {
                put16(dev, (maj << 8) | min);
                ok = emit_inode(img, cino, &st, 2, 0, dev, 2);
            } else {
                put32(dev, (min & 0xff) | (maj << 8) | ((min & ~0xffu) << 12));
                ok = emit_inode(img, cino, &st, 4, 0, dev, 4);
            }
        } else {
            ok = emit_inode(img, cino, &st, 0, 0, NULL, 0);
        }
    }
    closedir(d);
    return ok;
}

jffs2_img_t *jffs2_new(size_t erasesize, size_t limit) {
    jffs2_img_t *img = calloc(1, sizeof(*img));
    if (!img)
        return NULL;
    img->erasesize = erasesize;
    img->limit = limit;
    img->next_ino = 2; // 1 is the root
    return img;
}

void jffs2_free(jffs2_img_t *img) {
    if (img) {
        free(img->buf);
        free(img);
    }
}

bool jffs2_add_tree(jffs2_img_t *img, const char *path,
                    const char *skip_contents) {
    struct stat st;
    if (lstat(path, &st) || !S_ISDIR(st.st_mode))
        return fail(img, "%s is not a directory", path);
    // The root's own inode, so its mode and owner are the ones it had
    return emit_inode(img, 1, &st, 0, 0, NULL, 0) &&
           add_dir(img, 1, path, "", skip_contents);
}

const uint8_t *jffs2_data(jffs2_img_t *img, size_t *len) {
    size_t whole = (img->len + img->erasesize - 1) / img->erasesize *
                   img->erasesize;
    if (whole > img->len && reserve(img, whole - img->len)) {
        // The tail of the last block stays erased: free space to jffs2
        img->len = whole;
    }
    *len = img->len;
    return img->buf;
}

const char *jffs2_error(jffs2_img_t *img) { return img->err; }

void jffs2_counts(jffs2_img_t *img, int *entries, int *xattrs_dropped) {
    *entries = img->entries;
    *xattrs_dropped = img->xattrs;
}
