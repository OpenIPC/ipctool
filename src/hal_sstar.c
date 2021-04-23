#include "hal_sstar.h"

#include <string.h>

#include "chipid.h"
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
