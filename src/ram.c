#include <stdio.h>
#include <string.h>

#include "chipid.h"
#include "hal_hisi.h"
#include "hal_xm.h"
#include "ram.h"
#include "tools.h"

typedef struct meminfo {
    unsigned long MemTotal;
} meminfo_t;
meminfo_t mem;

static void parse_meminfo(struct meminfo *g) {
    char buf[60];
    FILE *fp;
    int seen_cached_and_available_and_reclaimable;

    fp = fopen("/proc/meminfo", "r");
    g->MemTotal = 0;
    while (fgets(buf, sizeof(buf), fp)) {
        if (sscanf(buf, "MemTotal: %lu %*s\n", &g->MemTotal) == 1)
            break;
    }
    fclose(fp);
}

void linux_mem() { parse_meminfo(&mem); }

unsigned long kernel_mem() { return mem.MemTotal; }

uint32_t rounded_num(uint32_t n) {
    int i;
    for (i = 0; n; i++) {
        n /= 2;
    }
    return 1 << i;
}

void hal_ram(unsigned long *media_mem, uint32_t *total_mem) {
    linux_mem();
    if (!strcmp(VENDOR_HISI, chip_manufacturer))
        *total_mem = hisi_totalmem(media_mem);
    else if (!strcmp(VENDOR_XM, chip_manufacturer))
        *total_mem = xm_totalmem(media_mem);

    if (!*total_mem)
        *total_mem = kernel_mem();
}

cJSON *detect_ram() {
    cJSON *fake_root = cJSON_CreateObject();
    cJSON *j_inner = cJSON_CreateObject();
    cJSON_AddItemToObject(fake_root, "ram", j_inner);

    unsigned long media_mem = 0;
    uint32_t total_mem = 0;
    hal_ram(&media_mem, &total_mem);
    ADD_PARAM_FMT("total", "%uM", rounded_num(total_mem / 1024));

    if (media_mem)
        ADD_PARAM_FMT("media", "%luM", media_mem / 1024);

    return fake_root;
}
