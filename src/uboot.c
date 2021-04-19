#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "mtd.h"
#include "uboot.h"

static uint32_t crc32_for_byte(uint32_t r) {
    for (int j = 0; j < 8; ++j)
        r = (r & 1 ? 0 : (uint32_t)0xEDB88320L) ^ r >> 1;
    return r ^ (uint32_t)0xFF000000L;
}

/* Any unsigned integer type with at least 32 bits may be used as
 * accumulator type for fast crc32-calulation, but unsigned long is
 * probably the optimal choice for most systems. */
typedef unsigned long accum_t;

static void init_tables(uint32_t *table, uint32_t *wtable) {
    for (size_t i = 0; i < 0x100; ++i)
        table[i] = crc32_for_byte(i);
    for (size_t k = 0; k < sizeof(accum_t); ++k)
        for (size_t w, i = 0; i < 0x100; ++i) {
            for (size_t j = w = 0; j < sizeof(accum_t); ++j)
                w = table[(uint8_t)(j == k ? w ^ i : w)] ^ w >> 8;
            wtable[(k << 8) + i] = w ^ (k ? wtable[0] : 0);
        }
}

static void crc32(const void *data, size_t n_bytes, uint32_t *crc) {
    static uint32_t table[0x100], wtable[0x100 * sizeof(accum_t)];
    size_t n_accum = n_bytes / sizeof(accum_t);
    if (!*table)
        init_tables(table, wtable);
    for (size_t i = 0; i < n_accum; ++i) {
        accum_t a = *crc ^ ((accum_t *)data)[i];
        for (size_t j = *crc = 0; j < sizeof(accum_t); ++j)
            *crc ^= wtable[(j << 8) + (uint8_t)(a >> 8 * j)];
    }
    for (size_t i = n_accum * sizeof(accum_t); i < n_bytes; ++i)
        *crc = table[(uint8_t)*crc ^ ((uint8_t *)data)[i]] ^ *crc >> 8;
}

#define CRC_SZ 4

// By default use 0x10000 but then can be changed after detection
size_t env_len = 0x10000;

// Detect U-Boot environment area offset
int uboot_detect_env(void *buf, size_t len) {
    size_t possible_lens[] = {0x10000, 0x40000};

    // Jump over memory by step
    int scan_step = 0x10000;

    for (int baddr = 0; baddr < len; baddr += scan_step) {
        uint32_t expected_crc = *(int *)(buf + baddr);

        for (int i = 0; i < sizeof(possible_lens) / sizeof(possible_lens[0]);
             i++) {
            if (possible_lens[i] + baddr > len)
                continue;

#if 1
            printf("Detecting at %#x with len %#x CRC is %#x\n", baddr,
                   possible_lens[i], *(int *)(buf + baddr));
#endif

            uint32_t res_crc = 0;
            crc32(buf + baddr + CRC_SZ, possible_lens[i] - CRC_SZ, &res_crc);
            if (res_crc == expected_crc) {
                env_len = possible_lens[i];
                return baddr;
            }
        }
    }

    return -1;
}

// Print environment configuration
void uboot_printenv(const char *env) {
    const char *ptr = env + CRC_SZ;
    while (*ptr) {
        puts(ptr);
        ptr += strlen(ptr) + 1;
    }
}

static void *uenv;
void uboot_copyenv(const void *buf) {
    if (!uenv) {
        uenv = malloc(env_len);
        memcpy(uenv, buf, env_len);
    }
}

void uboot_freeenv() {
    if (uenv)
        free(uenv);
}

// Get environment variable
const char *uboot_getenv(const char *name) {
    if (!uenv)
        return NULL;

    char param[1024];
    snprintf(param, sizeof(param), "%s=", name);

    const char *ptr = uenv + CRC_SZ;
    while (*ptr) {
        if (strncmp(ptr, param, strlen(param)) == 0) {
            return ptr + strlen(param);
        }
        ptr += strlen(ptr) + 1;
    }
    return NULL;
}

static void uboot_setenv(int mtd, uint32_t offset, const char *env,
                         const char *key, const char *newvalue,
                         uint32_t erasesize, enum FLASH_OP fop) {
    const char *towrite;
    uint32_t res_crc = 0;

    uboot_copyenv(env);
    char *newenv = calloc(env_len, 1);

    const char *ptr = uenv + CRC_SZ;
    char *nptr = newenv + CRC_SZ;
    bool found = false;
    while (*ptr) {
        if (!strncmp(ptr, key, strlen(key))) {
            found = true;
            char *delim = strchr(ptr, '=');
            if (!delim) {
                fprintf(stderr, "Bad uboot parameter '%s\n'", ptr);
                goto bailout;
            }
            char *oldvalue = delim + 1;
            // check if old value has same size of new one
            if (strlen(newvalue) == strlen(oldvalue)) {
                if (!strcmp(newvalue, oldvalue)) {
                    switch (fop) {
                    case FOP_INTERACTIVE:
                        fprintf(stderr, "Nothing will be changed\n");
                    case FOP_RAM:
                        goto bailout;
                    case FOP_ROM:
                        towrite = uenv;
                        goto rewrite;
                    }
                }
                memcpy(oldvalue, newvalue, strlen(newvalue));
                towrite = uenv;
                goto rewrite;
            } else {
                if (strlen(newvalue) != 0)
                    nptr += snprintf(nptr, env_len - (nptr - newenv), "%s=%s",
                                     key, newvalue) +
                            1;
            }
        } else {
            // copy as-is
            memcpy(nptr, ptr, strlen(ptr));
            nptr += strlen(ptr) + 1;
        }
        ptr += strlen(ptr) + 1;
    }
    if (!found) {
        // Need to add new value
        snprintf(nptr, env_len - (nptr - newenv), "%s=%s", key, newvalue);
    }
    towrite = newenv;

rewrite:
    if (fop == FOP_INTERACTIVE || fop == FOP_ROM) {
        crc32(towrite + CRC_SZ, env_len - CRC_SZ, &res_crc);
        *(uint32_t *)towrite = res_crc;
        mtd_write(mtd, offset, erasesize, towrite, env_len);
    }
    if (uenv != towrite)
        memcpy(uenv, towrite, env_len);

bailout:
    if (newenv)
        free(newenv);
}

#define OP_PRINTENV 0
#define OP_SETENV 1

typedef struct {
    int op;
    const char *key;
    const char *value;
    enum FLASH_OP fop;
} ctx_uboot_t;

static bool cb_uboot_env(int i, const char *name, struct mtd_info_user *mtd,
                         void *ctx) {
    int fd;
    char *addr = open_mtdblock(i, &fd, mtd->size, 0);
    if (!addr)
        return true;

    ctx_uboot_t *c = (ctx_uboot_t *)ctx;
    if (i < ENV_MTD_NUM) {
        size_t u_off = uboot_detect_env(addr, mtd->size);
        if (u_off != -1) {
            switch (c->op) {
            case OP_PRINTENV:
                uboot_printenv(addr + u_off);
                break;
            case OP_SETENV:
                uboot_setenv(i, u_off, addr + u_off, c->key, c->value,
                             mtd->erasesize, c->fop);
                break;
            }
            close(fd);
            return false;
        }
    }

    close(fd);
    return true;
}

void printenv() {
    ctx_uboot_t ctx;
    ctx.op = OP_PRINTENV;
    enum_mtd_info(&ctx, cb_uboot_env);
}

void set_env_param(const char *key, const char *value, enum FLASH_OP fop) {
    ctx_uboot_t ctx;
    ctx.op = OP_SETENV;
    ctx.key = key;
    ctx.value = value;
    ctx.fop = fop;
    enum_mtd_info(&ctx, cb_uboot_env);
}

void cmd_set_env(char *arg) {
    char *delim = strchr(arg, '=');
    if (!delim || delim == arg) {
        printf("Usage: setenv key=name\n");
        exit(2);
    }
    *delim = 0;
    set_env_param(arg, delim + 1, FOP_INTERACTIVE);
}
