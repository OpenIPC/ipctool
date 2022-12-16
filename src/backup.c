#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
// for strnlen

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <netdb.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "backup.h"
#include "chipid.h"
#include "cjson/cJSON.h"
#include "dns.h"
#include "hal_common.h"
#include "http.h"
#include "mtd.h"
#include "network.h"
#include "sha1.h"
#include "tools.h"
#include "uboot.h"
#include "vendors/common.h"
#include "vendors/xm.h"

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

int save_file(const char *filename, span_t blocks[MAX_MTDBLOCKS + 1],
              size_t blocks_num) {
    FILE *fp = fopen(filename, "wb+");
    if (!fp) {
        fprintf(stderr, "Error writing '%s', aborting\n", filename);
        return 1;
    }

    size_t len = 0;
    for (int i = 0; i < blocks_num; i++) {
        len += blocks[i].len;
        // add len header
        if (i)
            len += sizeof(uint32_t);
    }

    for (int i = 0; i < blocks_num; i++) {
        if (i) {
            uint32_t len_header = blocks[i].len;
            fwrite(&len_header, 1, sizeof(len_header), fp);
        }

        int nbytes = fwrite(blocks[i].data, 1, blocks[i].len, fp);
        if (nbytes == -1)
            break;
#if 0
        printf("[%d] written %d bytes\n", i, nbytes);
#endif
    }

    fclose(fp);

    return 0;
}

#define FILL_NS                                                                \
    nservers_t ns;                                                             \
    ns.len = 0;                                                                \
    parse_resolv_conf(&ns);                                                    \
    add_predefined_ns(&ns, 0xd043dede /* 208.67.222.222 of OpenDNS */,         \
                      0x01010101 /* 1.1.1.1 of Cloudflare */, 0);

int do_backup(const char *yaml, size_t yaml_len, bool wait_mode,
              const char *filename) {
    FILL_NS;

    char mac[32];
    if (!get_mac_address(mac, sizeof mac)) {
        return 10;
    };

    span_t blocks[MAX_MTDBLOCKS + 1];
    blocks[0].data = yaml;
    blocks[0].len = yaml_len + 1; // end string data with \0
    size_t bl_num = map_mtdblocks(blocks + 1, MAX_MTDBLOCKS) + 1;

    int ret;
    if (filename)
        ret = save_file(filename, blocks, bl_num);
    else
        ret = upload(mybackups, mac, &ns, blocks, bl_num);

    // don't release UDP lock for 30 days to prevent to do often snapshots
    if (!filename && !wait_mode)
        sleep(60 * 60 * 24 * 30);
    return ret;
}

char *download_backup(const char *mac, size_t *size, char *date) {
    FILL_NS;

    printf("Downloading %s firmware\n", mac);

    char *dwres = download(mybackups, mac, downcode, &ns, size, date, true);
    int err = HTTP_ERR(dwres);
    if (err) {
        switch (err) {
        case ERR_MALLOC:
            printf(
                "Tried to allocate %dMb\n"
                "Not enough memory, consider increasing osmem U-Boot param\n",
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
    size_t off_flashb;
    size_t size;
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
                    mi[i].off_flashb = offset;
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

#define MAX_NAME 32
typedef struct {
    ssize_t totalsz;
    size_t erasesize;
    struct {
        char name[MAX_NAME];
        size_t size;
    } part[MAX_MTDBLOCKS];
    bool skip_env;
    int env_dev;
    size_t env_offset;
} mtd_restore_ctx_t;

static size_t mtd_size_by_name(mtd_restore_ctx_t *ctx, const char *name) {
    for (int i = 0; i < MAX_MTDBLOCKS; i++) {
        if (!strcmp(ctx->part[i].name, name))
            return ctx->part[i].size;
    }
    return -1;
}

static bool cb_mtd_restore(int i, const char *name, struct mtd_info_user *mtd,
                           void *ctx) {
    mtd_restore_ctx_t *c = (mtd_restore_ctx_t *)ctx;
    if (!c->erasesize)
        c->erasesize = mtd->erasesize;

    if (c->env_dev == -1 && i < ENV_MTD_NUM) {
        int fd;
        char *addr = open_mtdblock(i, &fd, mtd->size, 0);
        if (!addr)
            return true;
        size_t u_off = uboot_detect_env(addr, mtd->size, mtd->erasesize);
        if (u_off != -1) {
            c->env_dev = i;
            c->env_offset = u_off;
            uboot_copyenv_int(addr + u_off);
        }
        close(fd);
    }

    strncpy(c->part[i].name, name, MAX_NAME);
    c->part[i].size = mtd->size;
    c->totalsz += mtd->size;
    return true;
}

static bool try_umount(const char *path) {
    for (int i = 0; i < 3; i++) {
        if (i != 0) {
            fprintf(stderr, ", trying again...\n");
            sleep(3);
        }
        int ret = umount2(path, MNT_FORCE | MNT_DETACH);
        if (ret == 0)
            return true;
        // Consider 'No such file or directory' as already done
        if (errno == 2)
            return true;
        fprintf(stderr, "Cannot umount '%s' because of %s (%d)", path,
                strerror(errno), errno);
    }
    return false;
}

static void umount_fs(const char *path) {
    // skip root
    if (!strcmp(path, "/"))
        return;

    if (!try_umount(path)) {
        fprintf(stderr, ", aborting...\n");
        exit(1);
    } else
        printf("Unmounting %s\n", path);
}

static bool umount_all() {
    sync();

    FILE *fp = fopen("/proc/mounts", "r");
    if (!fp)
        return false;

    char mount[80];
    while (fgets(mount, sizeof mount, fp)) {
        char dev[80], path[80], fs[80], attrs[80];
        int n;

        if (sscanf(mount, "%s %s %s %s", dev, path, fs, attrs)) {
            if (!strncmp(dev, "/dev/mtdblock", 13) && strstr(attrs, "rw"))
                umount_fs(path);
            else if (!strcmp(fs, "squashfs") || (!strcmp(fs, "cramfs")))
                umount_fs(path);
        }
    }

    fclose(fp);
    sync();
    return true;
}

static void print_flash_progress(int cur, int max, char status) {
    char *bar = alloca(max) + 1;
    for (int i = 0; i < max; i++) {
        if (cur == i)
            bar[i] = status;
        else if (i < cur)
            bar[i] = 'w';
        else
            bar[i] = '.';
    }
    bar[max] = 0;
    printf("Flashing [%s]\r", bar);
    fflush(stdout);
}

static int map_old_new_mtd(int old_num, size_t old_offset, size_t *new_offset,
                           stored_mtd_t *mtdbackup, mtd_restore_ctx_t *mtd) {
    size_t find_off = mtdbackup[old_num].off_flashb + old_offset;
    size_t cur_off = 0;
    for (int i = 0; i < MAX_MTDBLOCKS; i++) {
        if (!mtd->part[i].size)
            return -1;
        if (mtd->part[i].size + cur_off > find_off) {
            *new_offset = find_off - cur_off;
            // printf("[%.8x] Map %d,%.8x -> %d,%.8x\n", find_off, old_num,
            // find_off,
            //       i, *new_offset);
            return i;
        }
        cur_off += mtd->part[i].size;
    }
    return -1;
}

static bool do_flash(const char *phase, stored_mtd_t *mtdbackup,
                     mtd_restore_ctx_t *mtd, bool skip_env, bool simulate) {
    for (int i = 0; i < MAX_MTDBLOCKS; i++) {
        if (!*mtdbackup[i].name)
            continue;

        printf("%s %s\n", phase, mtdbackup[i].name);
        size_t chunk = mtd->erasesize;
        int cnt = mtdbackup[i].size / chunk;
        for (int c = 0; c < cnt; c++) {
            size_t this_offset;
            int newi =
                map_old_new_mtd(i, c * chunk, &this_offset, mtdbackup, mtd);
            if (newi == -1) {
                fprintf(stderr, "\nOffset algorithm error, aborting...\n");
                return false;
            }
            char op = 'e';
            if (skip_env && mtd->env_dev == newi &&
                mtd->env_offset == this_offset)
                op = 's';
            if (!simulate) {
                print_flash_progress(c, cnt, op);
                if (op != 's') {
#if 0
                    printf("mtd_write(%d, %x, %x, %p, %zx)\n", newi,
                           this_offset, mtd->erasesize,
                           mtdbackup[i].data + c * chunk, chunk);
#endif
                    if (!mtd_write(newi, this_offset, mtd->erasesize,
                                   mtdbackup[i].data + c * chunk, chunk)) {
                        fprintf(stderr,
                                "\nSomething went wrong, aborting...\n");
                        return false;
                    }
                }
            }
        }
        if (!simulate) {
            print_flash_progress(cnt, cnt, 'e');
            printf("\n");
        }
    }

    return true;
}

static bool free_resources(bool force) {
    if (is_xm_board()) {
        if (!xm_kill_stuff(force)) {
            fprintf(stderr, "aborting...\n");
            return false;
        }
    }
    umount_all();
    return true;
}

static void reboot_with_msg() {
    printf("System will be restarted...\n");
    reboot(RB_AUTOBOOT);
}

static int restore_backup(const char *arg, bool skip_env, bool force) {
    const char *uboot_env = "  U-Boot env overwrite will be skipped";
    printf("Restoring the backup\n%s\n", skip_env ? uboot_env : "");

    if (!free_resources(force))
        return 1;

    mtd_restore_ctx_t mtd;
    memset(&mtd, 0, sizeof(mtd));
    if (skip_env) {
        mtd.skip_env = skip_env;
        mtd.env_dev = -1;
    }
    enum_mtd_info(&mtd, cb_mtd_restore);
    if (skip_env) {
        if (mtd.env_dev != -1)
            printf("U-Boot env detected at #%d/0x%x\n", mtd.env_dev,
                   mtd.env_offset);
        else {
            fprintf(stderr, "No U-Boot env found, aborting...\n");
            return -1;
        }
    }

    size_t size;
    char date[DATE_BUF_LEN] = {0};
    char *backup;
    if (arg == NULL) {
        char mac[32];
        if (!get_mac_address(mac, sizeof mac))
            return 1;
        fprintf(stderr, "Downloading latest backup from the cloud\n");
        backup = download_backup(mac, &size, date);
    } else {
        if (access(arg, 0)) {
            fprintf(stderr,
                    "Downloading backup from the cloud by specified name\n");
            backup = download_backup(arg, &size, date);
        } else {
            fprintf(stderr, "Loading backup from file %s...\n", arg);

            backup = file_to_buf(arg, &size);
        }
    }

    if (backup) {
        char *pptr = backup + strnlen(backup, size) + 1;
        if (pptr - backup >= size) {
            fprintf(stderr, "Broken description found, aborting...\n");
            goto bailout;
        }

        if (*date)
            printf("Found backup made on %s\n", date);

        if (!force) {
            char c;
            fprintf(stderr, "Are you sure to proceed? (y/n)? ");
            scanf(" %c", &c);
            if (c != 'y')
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
                fprintf(stderr, "Broken backup: next block len 0x%x != 0x%zx\n",
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
            char digest[21] = {0};
            if (*mtdbackup[i].sha1) {
                printf("Checking %s... %32s\r", mtdbackup[i].name, "");
                fflush(stdout);
                SHA1(digest, pptr, blen);
                sha1 = ntohl(*(uint32_t *)&digest);
                snprintf(digest, sizeof(digest), "%.8x", sha1);
                if (strcmp(digest, mtdbackup[i].sha1)) {
                    fprintf(stderr,
                            "SHA1 digest differs for '%s', aborting...\n",
                            mtdbackup[i].name);
                    goto bailout;
                }
            } else {
                printf("Not checked (no SHA1)\r");
                fflush(stdout);
            }
#if 1
            fprintf(stderr, "\n[%d] 0x%.8zx\t0x%.8zx\t%8.8s\t%.8s\n", i,
                    mtdbackup[i].off_flashb, mtdbackup[i].size,
                    *mtdbackup[i].sha1 ? mtdbackup[i].sha1 : "n/a", digest);
#endif
            // end of sha1 check

            pptr += blen;
        }
        if (tsize != mtd.totalsz) {
            fprintf(stderr,
                    "Broken backup: backup size: 0x%x, real flash size: 0x%x\n",
                    tsize, mtd.totalsz);
            char c;
            fprintf(stderr, "Are you sure to proceed? (y/n)? ");
            scanf(" %c", &c);
            if (c != 'y')
                goto bailout;
        }
        printf("Backups were checked\n");

        if (!do_flash("Analyzing", mtdbackup, &mtd, skip_env, true))
            goto bailout;
        if (!do_flash("Restoring", mtdbackup, &mtd, skip_env, false))
            goto bailout;

        reboot_with_msg();

    bailout:

        free(backup);
    }
    return 0;
}

#define MAX_MTDPARTS 1024
static void add_mtdpart(char *dst, const char *name, uint32_t size) {
    size_t len = strlen(dst);
    bool need_comma = false;
    char prev = dst[len - 1];
    if (len && prev != ':' && prev != '=' && prev != ',')
        need_comma = true;
    snprintf(dst + len, MAX_MTDPARTS - len, "%s%dk(%s)", need_comma ? "," : "",
             size / 1024, name);
}

#define ASSERT_JSON(obj)                                                       \
    if (!obj) {                                                                \
        const char *error_ptr = cJSON_GetErrorPtr();                           \
        if (error_ptr) {                                                       \
            fprintf(stderr, "Error before: %s\n", error_ptr);                  \
        }                                                                      \
        ret = 1;                                                               \
        goto bailout;                                                          \
    }

#define JSON_CHECK(obj, type)                                                  \
    if (!obj || !cJSON_Is##type(obj)) {                                        \
        fprintf(stderr, "Error parsing json\n");                               \
        ret = 2;                                                               \
        goto bailout;                                                          \
    }

static uint32_t platform_ramstart() {
    switch (chip_generation) {
    case HISI_V1:
    case HISI_V2A:
    case HISI_V2:
    case HISI_V3A:
    case HISI_V3:
    case HISI_V4A:
        return 0x82000000;
    case HISI_V4:
        return 0x42000000;
    default:
        return 0;
    }
}

static void handle_signal(int signal) {
    printf("Ignore %s\n", strsignal(signal));
}

static void skip_signals() {
    struct sigaction sa = {
        .sa_handler = &handle_signal,
        // Restart the system call, if at all possible
        .sa_flags = SA_RESTART,
    };
    // Block every signal during the handler
    sigfillset(&sa.sa_mask);
    sigaction(SIGHUP, &sa, NULL);
    // Danger area, use only in production for sure
#if 1
    sigaction(SIGKILL, &sa, NULL);
#endif
}

static int do_upgrade(const char *filename, bool force) {
    if (!getchipname()) {
        fprintf(stderr, "Chip has not been detected\n");
        return 1;
    }

    cJSON *binfo = get_board_info();
    char board[1024] = {0}, board_id[1024] = {0};
    cJSON *c_vendor = cJSON_GetObjectItem(binfo, "vendor");
    if (c_vendor) {
        char *bstr = cJSON_GetStringValue(binfo);
        if (bstr)
            strcpy(board, bstr);
    }
    cJSON *c_model = cJSON_GetObjectItem(binfo, "model");
    if (c_model) {
        char *bstr = cJSON_GetStringValue(binfo);
	strcpy(board_id, bstr);
        strcat(board, " ");
        strcat(board, bstr);
    }
    if (binfo)
        cJSON_Delete(binfo);

    uint32_t ram_start = platform_ramstart();
    if (!ram_start) {
        fprintf(stderr, "Platform unknown.\n");
        return 1;
    }

    if (!free_resources(force))
        return 1;

    skip_signals();

    mtd_restore_ctx_t mtd = {.env_dev = -1};
    enum_mtd_info(&mtd, cb_mtd_restore);
    if (mtd.env_dev == -1) {
        fprintf(stderr, "Cannot find env, internal error\n");
        return 1;
    }

    stored_mtd_t mtdwrite[MAX_MTDBLOCKS] = {0};
    char mtdparts[MAX_MTDPARTS] = {0};

    int ret = 0;
    size_t len;
    if (!filename) {
        fprintf(
            stderr,
            "Cloud support is not available yet, please specify bundle path\n");
        return 1;
    } else
        printf("Using '%s' as upgrade bundle\n", filename);
    char *jsond = file_to_buf(filename, &len);
    if (!jsond) {
        fprintf(stderr, "'%s' is not found\n", filename);
        return 1;
    }

    int curr_mtd_part = 0;
    size_t json_size = strlen(jsond);
    cJSON *json = cJSON_ParseWithLength(jsond, json_size);
    ASSERT_JSON(json);

    char *read_ptr = jsond + json_size + 1;

    const cJSON *mtdparts_pr =
        cJSON_GetObjectItemCaseSensitive(json, "mtdPrefix");
    if (mtdparts_pr && cJSON_IsString(mtdparts_pr)) {
        strcpy(mtdparts, mtdparts_pr->valuestring);
        const char *ptr = mtdparts;
        bool comma_encounted = false;
        int part_len = 0;
        while (*ptr) {
            if (*ptr == ':')
                comma_encounted = true;
            else if (comma_encounted) {
                if (*ptr == ',')
                    part_len = 0;
                else {
                    if (part_len == 1)
                        curr_mtd_part++;
                    part_len++;
                }
            }
            ptr++;
        }
    }

    const cJSON *setTotalMem =
        cJSON_GetObjectItemCaseSensitive(json, "setTotalMem");
    char total_mem[32] = {0};
    if (setTotalMem && cJSON_IsBool(setTotalMem) && cJSON_IsTrue(setTotalMem)) {
        unsigned long mmem;
        uint32_t tmem;
        hal_ram(&mmem, &tmem);
        snprintf(total_mem, sizeof(total_mem), "%uM", rounded_num(tmem / 1024));
    }

    char kernel_mem[32] = {0};
    const cJSON *kernelMem =
        cJSON_GetObjectItemCaseSensitive(json, "kernelMem");
    if (kernelMem && cJSON_IsString(kernelMem))
        strncpy(kernel_mem, kernelMem->valuestring, sizeof(kernel_mem));

    // offset from flash start
    uint32_t goff = 0;
    const cJSON *skip_envs = cJSON_GetObjectItemCaseSensitive(json, "skip");
    if (skip_envs && cJSON_IsArray(skip_envs)) {
        const cJSON *skip_env = NULL;
        cJSON_ArrayForEach(skip_env, skip_envs) {
            if (cJSON_IsString(skip_env)) {
                const char *name = skip_env->valuestring;
                size_t size = mtd_size_by_name(&mtd, name);
                if (size != -1) {
                    add_mtdpart(mtdparts, name, size);
                    curr_mtd_part++;
                    goff += size;
                    printf("skipped original partition %s sized %#x\n", name,
                           size);
                }
            }
        }
    }

    const cJSON *offset = cJSON_GetObjectItemCaseSensitive(json, "offset");
    if (offset && cJSON_IsString(offset)) {
        goff = strtol(offset->valuestring, 0, 16);
        printf("Using offset: %#X\n", goff);
    }
    uint32_t payload;

    const cJSON *parts = cJSON_GetObjectItemCaseSensitive(json, "partitions");
    ASSERT_JSON(parts);
    const cJSON *part;
    int i = 0;
    int root_part = -1;
    int kernel_part = -1;

    cJSON_ArrayForEach(part, parts) {
        cJSON *jname = cJSON_GetObjectItemCaseSensitive(part, "name");
        JSON_CHECK(jname, String);
        strcpy(mtdwrite[i].name, jname->valuestring);

        uint32_t psize = 0;
        cJSON *jpsize = cJSON_GetObjectItemCaseSensitive(part, "partitionSize");
        if (jpsize) {
            if (cJSON_IsNumber(jpsize))
                psize = jpsize->valueint;
            else if (cJSON_IsString(jpsize))
                psize = strtol(jpsize->valuestring, 0, 16);
        }

        mtdwrite[i].off_flashb = goff;
        cJSON *jfile = cJSON_GetObjectItemCaseSensitive(part, "file");
        if (jfile) {
            JSON_CHECK(jfile, String);
            mtdwrite[i].data =
                fread_to_buf(jfile->valuestring, &mtdwrite[i].size,
                             psize ? psize : mtd.erasesize, &payload);
            assert(mtdwrite[i].data);
            fprintf(stderr, "read %s into memory\n", jfile->valuestring);
        } else {
            cJSON *jpayload =
                cJSON_GetObjectItemCaseSensitive(part, "payloadSize");
            if (!jpayload || !cJSON_IsNumber(jpayload)) {
                fprintf(stderr, "Bad payload size\n");
                return 1;
            }
            payload = jpayload->valueint;
            mtdwrite[i].size = ceil_up(payload, psize ? psize : mtd.erasesize);
            mtdwrite[i].data = malloc(mtdwrite[i].size);
            if (mtdwrite[i].data == NULL) {
                fprintf(stderr, "Allocation error\n");
                return 1;
            }
            memcpy(mtdwrite[i].data, read_ptr, payload);
            memset(mtdwrite[i].data + payload, 0xff,
                   mtdwrite[i].size - payload);
            read_ptr += payload;
        }

        if (psize && psize < mtdwrite[i].size) {
            fprintf(stderr,
                    "image 0x%zx doesn't fit to 0x%x partition, aborting...\n",
                    mtdwrite[i].size, psize);
            ret = 1;
            goto bailout;
        }
        add_mtdpart(mtdparts, mtdwrite[i].name, mtdwrite[i].size);
        if (!strcmp(mtdwrite[i].name, "rootfs")) {
            root_part = curr_mtd_part;
        } else if (!strcmp(mtdwrite[i].name, "kernel")) {
            kernel_part = i;
        }
        curr_mtd_part++;
        printf("\t%s\t%p, size: %zu bytes\n", mtdwrite[i].name,
               mtdwrite[i].data, mtdwrite[i].size);
        goff += mtdwrite[i].size;

        cJSON *jsha1 = cJSON_GetObjectItemCaseSensitive(part, "sha1");
        if (jsha1 && cJSON_IsString(jsha1)) {
            char digest[21] = {0};
            SHA1(digest, mtdwrite[i].data, payload);
            uint32_t sha1 = ntohl(*(uint32_t *)&digest);
            snprintf(digest, sizeof(digest), "%.8x", sha1);
            if (strcmp(digest, jsha1->valuestring)) {
                fprintf(stderr, "SHA1 digest differs for '%s', aborting...\n",
                        mtdwrite[i].name);
                ret = 3;
                goto bailout;
            }
        }

        if (!strcmp(mtdwrite[i].name, "boot")) {
            // Copy existing env just right after boot as separate partition
            size_t env_len;
            char *env = uboot_fullenv(&env_len);

            i++;

            mtd.env_dev =
                map_old_new_mtd(i, goff, &mtd.env_offset, mtdwrite, &mtd);

            mtdwrite[i].off_flashb = goff;
            mtdwrite[i].size = env_len;
            mtdwrite[i].data = env;
            strcpy(mtdwrite[i].name, "env");
            add_mtdpart(mtdparts, mtdwrite[i].name, mtdwrite[i].size);
            curr_mtd_part++;
            printf("\t%s\t%p, size: %zu bytes\n", mtdwrite[i].name,
                   mtdwrite[i].data, mtdwrite[i].size);
            goff += mtdwrite[i].size;
        }

        i++;
    }
    free(jsond);

    if (root_part == -1 || kernel_part == -1) {
        fprintf(stderr,
                "Cannot proceed with unknown kernel or root fs partition\n");
        return 1;
    }

    snprintf(mtdparts + strlen(mtdparts), MAX_MTDPARTS - strlen(mtdparts),
             ",-(rootfs_data)");

    if (!do_flash("Analyzing", mtdwrite, &mtd, false, true)) {
        printf("Early exit, check the logs\n");
        ret = 4;
        goto bailout;
    }
    if (!do_flash("Upgrading", mtdwrite, &mtd, false, false)) {
        printf("Early exit, check the logs\n");
        ret = 4;
        goto bailout;
    }

    char value[1024];

    cJSON *josmem = cJSON_GetObjectItemCaseSensitive(json, "osmem");
    if (josmem && cJSON_IsString(josmem))
        set_env_param_ram("osmem", josmem->valuestring);
    if (*total_mem) {
        set_env_param_ram("totalmem", total_mem);
        printf("set_env_param: total_mem=%s\n", total_mem);
    }
    if (*board_id)
        set_env_param_ram("hardware", board_id);
    set_env_param_ram("soc", getchipname());

    snprintf(value, sizeof(value),
             "setenv setargs setenv bootargs ${bootargs}; run setargs; "
             "sf probe 0; sf read 0x%x 0x%zx 0x%zx; "
             "bootm 0x%x",
             // kernel params
             ram_start, mtdwrite[kernel_part].off_flashb,
             mtdwrite[kernel_part].size, ram_start);
    set_env_param_ram("bootcmd", value);

    snprintf(value, sizeof(value),
             "mem=%s console=ttyAMA0,115200 panic=20 "
             "root=/dev/mtdblock%d rootfstype=squashfs "
             "mtdparts=%s",
             *kernel_mem ? kernel_mem : "${osmem}", root_part, mtdparts);
    cJSON *jaddcmdline =
        cJSON_GetObjectItemCaseSensitive(json, "additionalCmdline");
    if (jaddcmdline && cJSON_IsString(jaddcmdline))
        snprintf(value + strlen(value), sizeof(value) - strlen(value), " %s",
                 jaddcmdline->valuestring);
    set_env_param_rom("bootargs", value, mtd.env_dev, mtd.env_offset,
                      mtd.erasesize);
    reboot_with_msg();

    // only makes sense for memleak detection
bailout:
    cJSON_Delete(json);

    return ret;
}

extern void print_usage();

int upgrade_restore_cmd(int argc, char **argv) {
    const char *short_options = "fs";
    const struct option long_options[] = {
        {"force", no_argument, NULL, 'f'},
        {"skip-env", no_argument, NULL, 's'},
        {NULL, 0, NULL, 0},
    };
    bool skip_env = false;
    bool force = false;
    int res;
    int option_index;

    while ((res = getopt_long_only(argc, argv, "", long_options,
                                   &option_index)) != -1) {
        switch (res) {
        case 's':
            skip_env = true;
            break;
        case 'f':
            force = true;
            break;

        case '?':
            print_usage();
            return EXIT_FAILURE;
        }
    }

    const char *optarg = argv[optind];

    bool restore_mode = argv[0][0] == 'r';
    if (restore_mode)
        return restore_backup(optarg, skip_env, force);
    else
        return do_upgrade(optarg, force);
}
