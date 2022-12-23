#include "hal/bcm.h"

#include "hal/common.h"
#include "tools.h"

#include <string.h>

#ifndef STANDALONE_LIBRARY
#include "cjson/cJSON.h"
#endif

bool bcm_detect_cpu(char *chip_name) {
    char buf[256];

    if (!line_from_file("/proc/cpuinfo", "Hardware.+: (BCM[0-9-]+)", buf,
                        sizeof(buf)))
        return false;

    strcpy(chip_name, buf);

    return true;
}

#ifndef STANDALONE_LIBRARY
static void cpuinfo_param(cJSON *j_inner, char *name) {
    char out[256], pattern[256];

    snprintf(pattern, sizeof(pattern), "%s.+: (.+)", name);
    if (!line_from_file("/proc/cpuinfo", pattern, out, sizeof(out)))
        return;

    lsnprintf(pattern, sizeof(pattern), name);
    ADD_PARAM(pattern, out);
}

static void chip_properties(cJSON *j_inner) {
    cpuinfo_param(j_inner, "Revision");
    cpuinfo_param(j_inner, "Serial");
    cpuinfo_param(j_inner, "Model");
}
#endif

void bcm_setup_hal() {
#ifndef STANDALONE_LIBRARY
    hal_chip_properties = chip_properties;
#endif
}
