#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/mman.h>
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
} enum_mtd_ctx;

static bool cb_mtd_info(int i, const char *name, struct mtd_info_user *mtd,
                        void *ctx) {
    enum_mtd_ctx *c = (enum_mtd_ctx *)ctx;

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
    enum_mtd_ctx ctx;
    ctx.blocks = blocks;
    ctx.cap = bl_len;
    ctx.count = 0;

    enum_mtd_info(&ctx, cb_mtd_info);
    return ctx.count;
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

char *download_backup() {
    FILL_NS;

    char mac[32];
    if (!get_mac_address(mac, sizeof mac)) {
        return NULL;
    };

    int len;
    char *dwres = download(mybackups, mac, downcode, &ns, &len);
    int err = HTTP_ERR(dwres);
    if (err) {
        switch (err) {
        case ERR_MALLOC:
            printf("Tried to allocate %dMb\nNot enough memory\n",
                   len / 1024 / 1024);
            break;
        default:
            printf("Download error occured: %d\n", HTTP_ERR(dwres));
        }
        return NULL;
    } else {
        printf("Download is ok\n");
        return dwres;
    }
}

int restore_backup() {
    char *backup = download_backup();

    if (backup) {

        free(backup);
    }
    return 0;
}
