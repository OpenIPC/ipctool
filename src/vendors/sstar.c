#include "vendors/sstar.h"
#include "chipid.h"
#include "hal_sstar.h"
#include "tools.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum fmt_option_e {
    str_none = 0,
    str_rtrim = (1<<0),
    str_kebab_case = (1<<1),
} fmt_option_t;

static void strrtrim(char *str) {
    char *s = str;
    if (!str || !strlen(str))
        return;
    s = str + strlen(str) - 1;
    while (s > str && isspace(*s))
        *s-- = 0;
}

static void str_kebab(char *str)
{
    if(!str || !strlen(str))
        return;
    char *s = str;
    while(*s) {
        *s=tolower(*s);
        if(*s == '_')
            *s = '-';
        s++;
    }
}

static void append_board_param(char *fname, char *dst, char *fmt, fmt_option_t fmt_option) {
    char buf[255];
    FILE *fp = NULL;
    if (!access(fname, R_OK)) {
        if (fp = fopen(fname, "rb")) {
            memset(buf, 0, sizeof(buf));
            if (fread(buf, 1, sizeof(buf), fp) > 0) {
                if (fmt_option & str_rtrim)
                    strrtrim(buf);
                if (fmt_option & str_kebab_case)
                    str_kebab(buf);
                sprintf(dst + strlen(dst), fmt, buf);
            }
            fclose(fp);
        }
    }
}

void gather_sstar_board_info() {
    append_board_param("/sys/devices/soc0/machine", board_id, "%s", str_rtrim);
    append_board_param("/sys/class/mstar/msys/CHIP_ID", board_specific, "  %s", str_kebab_case);
    append_board_param("/sys/class/mstar/msys/CHIP_VERSION", board_specific, "  %s", str_kebab_case);
    append_board_param("/sys/devices/soc0/family", board_specific, "  soc-family: %s", 0);
    append_board_param("/sys/devices/soc0/soc_id", board_specific, "  soc-id: %s", 0);
    append_board_param("/sys/devices/soc0/revision", board_specific, "  soc-revision: %s", 0);
}

bool is_sstar_board() {
    bool ret = false;
    if (!access("/sys/devices/soc0/machine", R_OK)) {
        ret = true;
    }
    return ret;
}
