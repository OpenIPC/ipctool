#include "vendors/sstar.h"
#include "chipid.h"
#include "hal_sstar.h"
#include "tools.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void strrtrim(char *str) {
    char *s = str;
    if (!str || !strlen(str))
        return;
    s = str + strlen(str) - 1;
    while (s > str && isspace(*s))
        *s-- = 0;
}

static void append_board_param(char *fname, char *dst, char *fmt, int rtrim) {
    char buf[255];
    FILE *fp = NULL;
    if (!access(fname, R_OK)) {
        if (fp = fopen(fname, "rb")) {
            memset(buf, 0, sizeof(buf));
            if (fread(buf, 1, sizeof(buf), fp) > 0) {
                if (rtrim)
                    strrtrim(buf);
                sprintf(dst + strlen(dst), fmt, buf);
            }
            fclose(fp);
        }
    }
}

void gather_sstar_board_info() {
    append_board_param("/sys/devices/soc0/machine", board_id, "%s", 1);
    append_board_param("/sys/class/mstar/msys/CHIP_ID", board_specific, "  %s", 0);
    append_board_param("/sys/class/mstar/msys/CHIP_VERSION", board_specific, "  %s", 0);
    append_board_param("/sys/devices/soc0/family", board_specific, "  SoC_Family:%s", 0);
    append_board_param("/sys/devices/soc0/soc_id", board_specific, "  SoC_Id:%s", 0);
    append_board_param("/sys/devices/soc0/revision", board_specific, "  SoC_Revision:%s", 0);
}

bool is_sstar_board() {
    bool ret = false;
    if (!access("/sys/devices/soc0/machine", R_OK)) {
        ret = true;
    }
    return ret;
}
