#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <dirent.h>
#include <linux/limits.h>
#include <sys/types.h>
#include <unistd.h>

#include "cjson/cYAML.h"

#include "chipid.h"
#include "firmware.h"
#include "tools.h"
#include "uboot.h"

#define ADD_FIRMWARE(param, val)                                               \
    {                                                                          \
        cJSON *strval = cJSON_CreateString(val);                               \
        cJSON_AddItemToObject(j_firmware, param, strval);                      \
    }

#define ADD_FIRMWARE_FMT(param, fmt, ...)                                      \
    {                                                                          \
        char val[1024];                                                        \
        snprintf(val, sizeof(val), fmt, __VA_ARGS__);                          \
        cJSON *strval = cJSON_CreateString(val);                               \
        cJSON_AddItemToObject(j_firmware, param, strval);                      \
    }

static unsigned long time_by_proc(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp)
        return 0;

    char line[1024];
    if (!fgets(line, sizeof(line), fp))
        return 0;
    const char *rpar = strchr(line, ')');
    if (!rpar || *(rpar + 1) != ' ')
        return 0;

    char state;
    long long ppid, pgid, sid, tty_nr, tty_pgrp;
    unsigned long flags, min_flt, cmin_flt, maj_flt, cmaj_flt, utime, stime;
    long long cutime, cstime;
    sscanf(rpar + 2,
           "%c %lld %lld %lld %lld %lld %ld %ld %ld %ld %ld %ld %ld %lld %lld",
           &state, &ppid, &pgid, &sid, &tty_nr, &tty_pgrp, &flags, &min_flt,
           &cmin_flt, &maj_flt, &cmaj_flt, &utime, &stime, &cutime, &cstime);

    fclose(fp);
    return utime + stime;
}

static void get_god_app(cJSON *j_firmware) {
    DIR *dir = opendir("/proc");
    if (!dir)
        return;

    unsigned long max = 0;
    char sname[1024];
    pid_t godpid;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (*entry->d_name && isdigit(entry->d_name[0])) {
            snprintf(sname, sizeof(sname), "/proc/%s/stat", entry->d_name);

            unsigned long curres = time_by_proc(sname);
            if (curres > max) {
                max = curres;
                godpid = strtol(entry->d_name, NULL, 10);
            }
        }
    };
    closedir(dir);

    if (godpid) {
        snprintf(sname, sizeof(sname), "/proc/%d/cmdline", godpid);
        FILE *fp = fopen(sname, "r");
        if (!fp)
            return;
        if (!fgets(sname, sizeof(sname), fp))
            return;
        ADD_FIRMWARE("god-app", sname);

        fclose(fp);
    }
}

static void get_hisi_sdk(cJSON *j_firmware) {
    char buf[1024];

    if (get_regex_line_from_file("/proc/umap/sys", "Version: \\[(.+)\\]", buf,
                                 sizeof(buf))) {
        char *ptr = strchr(buf, ']');
        char *build = strchr(buf, '[');
        if (!ptr || !build)
            return;
        *ptr++ = ' ';
        *ptr++ = '(';
        strcpy(ptr, build + 1);
        strcat(ptr, ")");
        ADD_FIRMWARE("sdk", buf);
    }
}

static void get_kernel_version(cJSON *j_firmware) {
    FILE *fp = fopen("/proc/version", "r");
    if (!fp)
        return;

    char line[1024];
    if (!fgets(line, sizeof(line), fp))
        return;
    fclose(fp);

    const char *build = "";
    char *pound = strchr(line, '#');
    if (pound) {
        char *buildstr = strchr(pound, ' ');
        if (buildstr) {
            char *end = strchr(buildstr, '\n');
            if (end)
                *end = 0;
            build = buildstr + 1;
        }
    }

    char *ptr, *toolchain = NULL;
    int pars = 0, group = 0;
    for (ptr = line; *ptr; ptr++) {
        if (*ptr == '(') {
            pars++;
            if (group == 1 && pars == 1) {
                toolchain = ptr + 1;
            }
        } else if (*ptr == ')') {
            pars--;
            if (pars == 0) {
                group++;
                if (group == 2) {
                    *ptr = 0;
                    break;
                }
            }
        }
    }

    char *version = line;
    int spaces = 0;
    for (ptr = line; *ptr; ptr++) {
        if (*ptr == ' ') {
            spaces++;
            if (spaces == 2) {
                version = ptr + 1;
            } else if (spaces == 3) {
                *ptr = 0;
                break;
            }
        }
    }
    ADD_FIRMWARE_FMT("kernel", "%s (%s)", version, build);
    if (toolchain)
        ADD_FIRMWARE("toolchain", toolchain);
}

static void get_libc(cJSON *j_firmware) {
    char buf[PATH_MAX] = {0};

    if (readlink("/lib/libc.so.0", buf, sizeof(buf)) == -1)
        return;

    if (!strncmp(buf, "libuClibc-", 10)) {
        char *ver = buf + 10;
        for (char *ptr = ver + strlen(ver) - 1; ptr != ver; ptr--) {
            if (*ptr == '.') {
                *ptr = 0;
                break;
            }
        }
        ADD_FIRMWARE_FMT("libc", "uClibc %s", ver);
    }
}

bool detect_firmare() {
    cJSON *fake_root = cJSON_CreateObject();
    cJSON *j_firmware = cJSON_CreateObject();
    cJSON_AddItemToObject(fake_root, "firmware", j_firmware);

    const char *uver = uboot_getenv("ver");
    if (uver) {
        const char *stver = strchr(uver, ' ');
        if (stver && *(stver + 1)) {
            ADD_FIRMWARE("u-boot", stver + 1);
        }
    }

    get_kernel_version(j_firmware);
    get_libc(j_firmware);
    get_hisi_sdk(j_firmware);
    get_god_app(j_firmware);

    char *string = cYAML_Print(fake_root);
    puts(string);

    cJSON_Delete(fake_root);
    return true;
}
