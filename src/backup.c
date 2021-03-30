#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
// for strnlen

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "backup.h"
#include "dns.h"
#include "http.h"
#include "mtd.h"
#include "network.h"
#include "sha1.h"
#include "tools.h"

#define UDP_LOCK_PORT 1025

static char mybackups[] = "camware.s3.eu-north-1.amazonaws.com";
static const char *downcode = "reil9phiFahng8aiPh5Kooshag8eiVae";

bool udp_lock() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1)
        return false;

    struct sockaddr_in name;
    name.sin_family = AF_INET;
    name.sin_port = htons(UDP_LOCK_PORT);
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (struct sockaddr *)&name, sizeof(name)) < 0)
        return false;

    return true;
}

typedef struct {
    size_t count;
    span_t *blocks;
    size_t cap;
} mtd_backup_ctx;

static bool cb_mtd_backup(int i, const char *name, struct mtd_info_user *mtd,
                          void *ctx) {
    mtd_backup_ctx *c = (mtd_backup_ctx *)ctx;

    int fd;
    char *addr = open_mtdblock(i, &fd, mtd->size, 0);
    if (!addr)
        return true;

    c->blocks[c->count].data = addr;
    c->blocks[c->count].len = mtd->size;
    c->count++;
    return true;
}

static int map_mtdblocks(span_t *blocks, size_t bl_len) {
    mtd_backup_ctx mtd;
    mtd.blocks = blocks;
    mtd.cap = bl_len;
    mtd.count = 0;

    enum_mtd_info(&mtd, cb_mtd_backup);
    return mtd.count;
}

#define FILL_NS                                                                \
    nservers_t ns;                                                             \
    ns.len = 0;                                                                \
    parse_resolv_conf(&ns);                                                    \
    add_predefined_ns(&ns, 0xd043dede /* 208.67.222.222 of OpenDNS */,         \
                      0x01010101 /* 1.1.1.1 of Cloudflare */, 0);

int do_backup(const char *yaml, size_t yaml_len, bool wait_mode) {
    FILL_NS;

    char mac[32];
    if (!get_mac_address(mac, sizeof mac)) {
        return 10;
    };

    span_t blocks[MAX_MTDBLOCKS + 1];
    blocks[0].data = yaml;
    blocks[0].len = yaml_len + 1; // end string data with \0
    size_t bl_num = map_mtdblocks(blocks + 1, MAX_MTDBLOCKS) + 1;

    int ret = upload(mybackups, mac, &ns, blocks, bl_num);

    // don't release UDP lock for 30 days
    if (!wait_mode)
        sleep(60 * 60 * 24 * 30);
    return ret;
}

char *download_backup(size_t *size) {
    FILL_NS;

    char mac[32];
    if (!get_mac_address(mac, sizeof mac)) {
        return NULL;
    };

    char *dwres = download(mybackups, mac, downcode, &ns, size, true);
    int err = HTTP_ERR(dwres);
    if (err) {
        switch (err) {
        case ERR_MALLOC:
            printf("Tried to allocate %dMb\nNot enough memory\n",
                   *size / 1024 / 1024);
            break;
        default:
            printf("Download error occured: %d\n", HTTP_ERR(dwres));
        }
        return NULL;
    } else {
        printf("Download is ok \n");
        return dwres;
    }
}

static int yaml_idlvl(char *from, char *start) {
    int cnt = 0;
    while (start != --from) {
        if (*from != ' ')
            break;
        cnt++;
    }
    return cnt;
}

typedef struct {
    size_t mtd_offset;
    size_t file_offset;
    unsigned long size;
    char sha1[9];
    char name[64];
    char *data;
} stored_mtd_t;

static int yaml_parseblock(char *start, int indent, stored_mtd_t *mi) {
    char *ptr = start;
    char *param = NULL;
    bool linestart = true;
    int spaces = 0;
    int len = strlen(start);
    bool has_dash = false;

    int i = -1;
    int rootlvl = -1;
    size_t offset = 0;

    while (ptr < start + len) {
        if (linestart) {
            if (*ptr == '-')
                has_dash = true;
            if (*ptr == ' ' || *ptr == '-') {
                spaces++;
            } else {
                if (has_dash) {
                    if (rootlvl == -1)
                        rootlvl = spaces;
                    if (rootlvl == spaces) {
                        i++;
                        if (i == MAX_MTDBLOCKS)
                            break;
                    }
                }
                linestart = false;
                if (spaces <= indent)
                    break;
                param = ptr;
            }
        }
        if (*ptr == '\n') {
            if (param && spaces == rootlvl) {
                if (!strncmp(param, "size: ", 6)) {
                    mi[i].mtd_offset = offset;
                    mi[i].size = strtoul(param + 6, NULL, 16);
                    offset += mi[i].size;
                } else if (!strncmp(param, "name: ", 6)) {
                    memcpy(mi[i].name, param + 6,
                           MIN(ptr - param - 6, sizeof(mi[i]) - 1));
                } else if (!strncmp(param, "sha1: ", 6))
                    memcpy(mi[i].sha1, param + 6, MIN(ptr - param - 6, 8));
            }
            linestart = true;
            spaces = 0;
            has_dash = false;
        }
        ptr++;
    }
    return i + 1;
}

typedef struct {
    ssize_t totalsz;
    size_t erasesize;
} mtd_restore_ctx_t;

static bool cb_mtd_restore(int i, const char *name, struct mtd_info_user *mtd,
                           void *ctx) {
    mtd_restore_ctx_t *c = (mtd_restore_ctx_t *)ctx;
    if (!c->erasesize)
        c->erasesize = mtd->erasesize;
    c->totalsz += mtd->size;
    return true;
}

static bool umount_all() {
    FILE *fp = fopen("/proc/mounts", "r");
    if (!fp)
        return false;

    char mount[80];
    while (fgets(mount, sizeof mount, fp)) {
        char path[80], fs[80], attrs[80];
        int n;

        if (sscanf(mount, "/dev/mtdblock%d %s %s %s", &n, path, fs, attrs)) {
            if (strstr(attrs, "rw")) {
                if (umount(path) != 0)
                    fprintf(stderr, "Cannot umount '%s'\n", path);
                else
                    printf("Unmounting %s\n", path);
            }
        }
    }

    fclose(fp);
    return true;
}

int restore_backup() {
    umount_all();

    mtd_restore_ctx_t mtd;
    memset(&mtd, 0, sizeof(mtd));
    enum_mtd_info(&mtd, cb_mtd_restore);

    size_t size;
    char *backup = download_backup(&size);

    if (backup) {
        char *pptr = backup + strnlen(backup, size) + 1;
        if (pptr - backup >= size) {
            fprintf(stderr, "Broken description found, aborting...\n");
            goto bailout;
        }

        // TODO: sane YAML parser
        char *ps = strstr(backup, "partitions:\n");
        if (!ps) {
            fprintf(stderr, "Broken backup, aborting...\n");
            puts(backup);
            printf("len = %d, ptr = %p\n", size, backup);
            goto bailout;
        }

        stored_mtd_t mtdbackup[MAX_MTDBLOCKS];
        memset(&mtdbackup, 0, sizeof(mtdbackup));
        size_t tsize = 0;
        int n = yaml_parseblock(strchr(ps, '\n') + 1, yaml_idlvl(ps, backup),
                                mtdbackup);
        for (int i = 0; i < n; i++) {
            tsize += mtdbackup[i].size;

            // check SHA1
            size_t blen = read_le32(pptr);
            if (blen != mtdbackup[i].size) {
                fprintf(stderr, "Broken backup: next block len 0x%x != 0x%lx\n",
                        blen, mtdbackup[i].size);
                goto bailout;
            }
            pptr += 4;
            if (pptr + blen - backup > size) {
                fprintf(stderr, "Early backup end found, aborting...\n");
                goto bailout;
            }
            mtdbackup[i].data = pptr;

            uint32_t sha1;
            if (*mtdbackup[i].sha1) {
                printf("Checking %s... %32s\r", mtdbackup[i].name, "");
                fflush(stdout);
                char digest[21] = {0};
                SHA1(digest, pptr, blen);
                sha1 = ntohl(*(uint32_t *)&digest);
                snprintf(digest, sizeof(digest), "%.8x", sha1);
                if (strcmp(digest, mtdbackup[i].sha1)) {
                    fprintf(stderr,
                            "SHA1 digest differs for '%s', aborting...\n",
                            mtdbackup[i].name);
                    goto bailout;
                }
            }
#if 1
            fprintf(stderr, "\n[%d] 0x%.8zx\t0x%.8lx\t%s\t%.8x\n", i,
                    mtdbackup[i].mtd_offset, mtdbackup[i].size,
                    mtdbackup[i].sha1, sha1);
#endif
            // end of sha1 check

            pptr += blen;
        }
        if (tsize != mtd.totalsz) {
            fprintf(stderr,
                    "Broken backup: backup size: 0x%x, real flash size: 0x%x\n"
                    "aborting...\n",
                    tsize, mtd.totalsz);
            goto bailout;
        }
        printf("Backups were checked\n");

        // close all applications and umount rw partitions

        // actual restore
        for (int i = n - 2; i < n; i++) {
            printf("Restoring %s\n", mtdbackup[i].name);
            printf("mtd_write(%d, %x, %x, %p, %lx)\n", i, 0, mtd.erasesize,
                   mtdbackup[i].data, mtdbackup[i].size);
            if (mtd_write(i, 0, mtd.erasesize, mtdbackup[i].data,
                          mtdbackup[i].size)) {
                fprintf(stderr, "Something went wrong, aborting...\n");
                goto bailout;
            };
        }

    bailout:

        free(backup);
    }
    return 0;
}
