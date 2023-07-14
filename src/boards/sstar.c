#include "boards/sstar.h"
#include "chipid.h"
#include "hal/sstar.h"
#include "tools.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum fmt_option_e {
    str_none = 0,
    str_rtrim = (1 << 0),
    str_ltrim = (1 << 1),
} fmt_option_t;

static void strrtrim(char *str) {
    char *s = str;
    if (!str || !strlen(str))
        return;
    s = str + strlen(str) - 1;
    while (s > str && isspace(*s))
        *s-- = 0;
}

static char *strltrim(char *str) {
    if (!str || !strlen(str))
        return NULL;
    char *s = str;
    while (*s != 0) {
        if (isspace(*s))
            return s + 1;
        s++;
    }
    return str;
}

static void append_board_param(cJSON *j_inner, char *fname, char *param,
                               fmt_option_t fmt_option) {
    char buf[255];
    FILE *fp = NULL;
    if (!access(fname, R_OK)) {
        if ((fp = fopen(fname, "rb"))) {
            memset(buf, 0, sizeof(buf));
            if (fread(buf, 1, sizeof(buf), fp) > 0) {
                int n = strlen(buf);
                if (buf[n - 1] == '\n')
                    buf[n - 1] = 0;
                if (fmt_option & str_rtrim)
                    strrtrim(buf);

                if (fmt_option & str_ltrim)
                    ADD_PARAM(param, strltrim(buf));
                else
                    ADD_PARAM(param, buf);
            }
            fclose(fp);
        }
    }
}

bool gather_sstar_board_info(cJSON *j_inner) {
    append_board_param(j_inner, "/sys/devices/soc0/machine", "model",
                       str_rtrim);
    append_board_param(j_inner, "/sys/class/mstar/msys/CHIP_ID", "chip-id",
                       str_ltrim);
    append_board_param(j_inner, "/sys/class/mstar/msys/CHIP_VERSION",
                       "chip-version", str_ltrim);
    append_board_param(j_inner, "/sys/devices/soc0/family", "soc-family", 0);
    append_board_param(j_inner, "/sys/devices/soc0/soc_id", "soc-id", 0);
    append_board_param(j_inner, "/sys/devices/soc0/revision", "soc-revision",
                       0);
    return true;
}

bool is_sstar_board() {
    bool ret = false;
    if (!access("/sys/devices/soc0/machine", R_OK)) {
        ret = true;
    }
    return ret;
}
