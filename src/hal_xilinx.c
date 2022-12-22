#include "hal_xilinx.h"
#include "hal_common.h"
#include "tools.h"

#include <string.h>

#ifndef STANDALONE_LIBRARY
#include "cjson/cJSON.h"
#endif

/* Register PSS_IDCODE Details */
typedef struct {
    unsigned reserved : 1;
    unsigned manufacturer_id : 11;
    unsigned device : 5;
    unsigned subfamily : 4;
    unsigned family : 7;
    unsigned revision : 4;
} pss_idcode_t;

static bool is_zynq;

bool xilinx_detect_cpu(char *chip_name) {
    char buf[256];

    if (!get_regex_line_from_file("/proc/cpuinfo",
                                  "Hardware.+: Xilinx ([a-zA-Z0-9-]+)", buf,
                                  sizeof(buf)))
        return false;

    if (strcmp(buf, "Zynq")) {
        fprintf(stderr, "%s is not supported yet\n", buf);
    }

    is_zynq = true;
    pss_idcode_t idcode;
    if (mem_reg(0xF8000530, (uint32_t *)&idcode, OP_READ)) {
        if (!(idcode.family == 0x1b && idcode.subfamily == 0x9 &&
              idcode.manufacturer_id == 0x49 && idcode.reserved == 1)) {
            fprintf(stderr, "%s unexpected register values\n", buf);
            return false;
        }

        sprintf(chip_name, "%s ", buf);
        switch (idcode.device) {
        case 0x03:
            strcat(chip_name, "7z007");
            break;
        case 0x1c:
            strcat(chip_name, "7z012s");
            break;
        case 0x08:
            strcat(chip_name, "7z014s");
            break;
        case 0x02:
            strcat(chip_name, "7z010");
            break;
        case 0x1b:
            strcat(chip_name, "7z015");
            break;
        case 0x07:
            strcat(chip_name, "7z020");
            break;
        case 0x0c:
            strcat(chip_name, "7z030");
            break;
        case 0x12:
            strcat(chip_name, "7z035");
            break;
        case 0x11:
            strcat(chip_name, "7z045");
            break;
        case 0x16:
            strcat(chip_name, "7z100");
            break;
        }
        return true;
    }

    return false;
}

#ifndef STANDALONE_LIBRARY
typedef struct {
    unsigned reserved : 28;
    unsigned ps_version : 4;
} mctrl_t;

static void chip_properties(cJSON *j_inner) {
    mctrl_t mctrl;
    if (is_zynq && mem_reg(0xF8007080, (uint32_t *)&mctrl, OP_READ)) {
        ADD_PARAM_NUM("versionId", mctrl.ps_version);
    }
}
#endif

void setup_hal_xilinx() {
#ifndef STANDALONE_LIBRARY
    hal_chip_properties = chip_properties;
#endif
}
