#include "upgrade_env.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The word of `s` that starts with `key` (at the start or after a space),
 * or NULL. */
static const char *find_word(const char *s, const char *key) {
    for (const char *p = s; (p = strstr(p, key)); p++)
        if (p == s || p[-1] == ' ')
            return p;
    return NULL;
}

static size_t word_len(const char *w) { return strcspn(w, " "); }

bool upgrade_mtd_id(const char *bootargs, char *id, size_t idsz) {
    const char *w = bootargs ? find_word(bootargs, "mtdparts=") : NULL;
    if (!w)
        return false;
    w += strlen("mtdparts=");
    size_t n = strcspn(w, ": ");
    if (!n || w[n] != ':' || memchr(w, '$', n) || n >= idsz)
        return false;
    memcpy(id, w, n);
    id[n] = '\0';
    return true;
}

bool upgrade_bootargs(const char *old, int root_idx, const char *mtdparts,
                      char *out, size_t outsz) {
    char id[64];
    if (!old || !upgrade_mtd_id(old, id, sizeof(id)) ||
        !find_word(old, "root="))
        return false;

    size_t used = 0;
    const char *p = old;
    while (*p) {
        if (*p == ' ') {
            if (used + 1 >= outsz)
                return false;
            out[used++] = *p++;
            continue;
        }
        size_t n = word_len(p);
        int w;
        if (!strncmp(p, "root=", 5))
            w = snprintf(out + used, outsz - used, "root=/dev/mtdblock%d",
                         root_idx);
        else if (!strncmp(p, "mtdparts=", 9))
            w = snprintf(out + used, outsz - used, "mtdparts=%s", mtdparts);
        else
            w = snprintf(out + used, outsz - used, "%.*s", (int)n, p);
        if (w < 0 || (size_t)w >= outsz - used)
            return false;
        used += w;
        p += n;
    }
    out[used] = '\0';
    return true;
}

bool upgrade_bootcmd(const char *old, uint32_t off, uint32_t size, char *out,
                     size_t outsz) {
    if (!old)
        return false;
    const char *rd = strstr(old, "sf read ");
    if (!rd || strstr(rd + 1, "sf read "))
        return false;

    const char *p = rd + strlen("sf read ");
    char *end;
    strtoul(p, &end, 16); // the load address stays
    if (end == p || *end != ' ')
        return false;
    const char *addr_end = end;
    p = end + 1;
    strtoul(p, &end, 16);
    if (end == p || *end != ' ')
        return false;
    p = end + 1;
    strtoul(p, &end, 16);
    if (end == p)
        return false;

    int w = snprintf(out, outsz, "%.*s 0x%x 0x%x%s", (int)(addr_end - old),
                     old, off, size, end);
    return w >= 0 && (size_t)w < outsz;
}
