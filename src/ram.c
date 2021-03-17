#include <stdio.h>

#include "ram.h"

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
