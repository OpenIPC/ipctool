#include "hal_sstar.h"

#include <stdlib.h>
#include <string.h>

#include "chipid.h"
#include "hal_common.h"
#include "tools.h"

bool sstar_detect_cpu() {
    uint32_t val;
    if (mem_reg(0x1f003c00, &val, OP_READ)) {
        snprintf(chip_id, sizeof(chip_id), "id %#x", val);
        chip_generation = val;
        return true;
    }
    return false;
}

static unsigned long sstar_media_mem() {
    char buf[256];

    if (!get_regex_line_from_file("/proc/cmdline",
                                  "mma_heap=.+sz=(0x[0-9A-Fa-f]+)", buf,
                                  sizeof(buf)))
        return 0;
    return strtoul(buf, NULL, 16) / 1024;
}

unsigned long sstar_totalmem(unsigned long *media_mem) {
    *media_mem = sstar_media_mem();
    return *media_mem + kernel_mem();
}
