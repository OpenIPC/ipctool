#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

const int crc_sz = 4;
// Assume env sole size of 64Kb
const int env_len = 0x10000;

// Detect U-Boot environment area offset
int uboot_detect_env(void *buf, size_t len) {
    // Jump over memory by step
    int scan_step = 0x0010000;
    int res = -1;

    for (int baddr = 0; baddr < len; baddr += scan_step) {
        uint32_t expected_crc = *(int *)(buf + baddr);

#if 0
        printf("Detecting at 0x%x, CRC is 0x%x\n", baddr,
               *(int *)(buf + baddr));
#endif

        uint32_t res_crc = 0;
        crc32(buf + baddr + crc_sz, env_len - crc_sz, &res_crc);
        if (res_crc == expected_crc) {
            res = baddr;
            break;
        }
    }

    return res;
}

// Print environment configuration
void uboot_printenv(void *buf) {
    const char *ptr = buf + crc_sz;
    while (*ptr) {
        puts(ptr);
        ptr += strlen(ptr) + 1;
    }
}

static void *uenv;
void uboot_copyenv(void *buf) {
    uenv = malloc(env_len);
    memcpy(uenv, buf, env_len);
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

    const char *ptr = uenv + crc_sz;
    while (*ptr) {
        if (strncmp(ptr, param, strlen(param)) == 0) {
            return ptr + strlen(param);
        }
        ptr += strlen(ptr) + 1;
    }
    return NULL;
}
