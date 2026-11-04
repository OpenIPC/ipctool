#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mtd_alias.h"

void mtd_mark_aliases(int n, const uint64_t *off, const bool *has_off,
                      const uint64_t *size, bool *alias) {
    for (int i = 0; i < n; i++) {
        alias[i] = false;
        if (has_off[i]) {
            for (int j = 0; j < n; j++) {
                if (j == i || !has_off[j] || size[j] >= size[i])
                    continue;
                if (off[j] >= off[i] && off[j] + size[j] <= off[i] + size[i]) {
                    alias[i] = true;
                    break;
                }
            }
            continue;
        }
        uint64_t others = 0;
        for (int j = 0; j < n; j++)
            if (j != i)
                others += size[j];
        alias[i] = n > 1 && others == size[i];
    }
}

// The kernel's memparse(): strtoull with a size suffix
static uint64_t memparse(const char *s, const char **end) {
    char *e;
    uint64_t v = strtoull(s, &e, 0);
    if (e != s && *e && strchr("kKmMgG", *e)) {
        v <<= *e == 'k' || *e == 'K' ? 10 : *e == 'm' || *e == 'M' ? 20 : 30;
        e++;
    }
    *end = e;
    return v;
}

// Every partition of one <mtd-id>:<partdef>,... A part with no offset follows
// the one before it. Returns where the definition ends, or NULL if it is not
// one.
static const char *scan_mtddef(const char *s, const char *name, uint64_t size,
                               uint64_t *offset, int *found) {
    uint64_t next = 0;
    for (;;) {
        bool rest = *s == '-';
        uint64_t psize = 0, off = next;
        const char *e = s + 1;
        if (!rest) {
            psize = memparse(s, &e);
            if (e == s)
                return NULL;
        }
        s = e;
        if (*s == '@') {
            off = memparse(s + 1, &e);
            if (e == s + 1)
                return NULL;
            s = e;
        }
        if (*s == '(') {
            const char *close = strchr(s, ')');
            if (!close)
                return NULL;
            size_t n = close - s - 1;
            if (n == strlen(name) && !strncmp(s + 1, name, n) &&
                (rest || psize == size)) {
                if (*found && *offset != off)
                    *found = 2;
                else if (!*found)
                    *found = 1;
                *offset = off;
            }
            s = close + 1;
        }
        s += strcspn(s, ",; "); // the ro and lk flags
        next = off + psize;
        if (*s != ',')
            return s;
        s++;
    }
}

bool mtd_offset_from_cmdline(const char *cmdline, const char *name,
                             uint64_t size, uint64_t *offset) {
    // The kernel keeps the last one it is given
    const char *def = NULL;
    for (const char *p = cmdline; (p = strstr(p, "mtdparts=")); p++)
        if (p == cmdline || p[-1] == ' ')
            def = p + strlen("mtdparts=");
    if (!def)
        return false;

    int found = 0;
    while (def && *def && *def != ' ') {
        const char *colon = strchr(def, ':');
        if (!colon)
            break;
        def = scan_mtddef(colon + 1, name, size, offset, &found);
        if (def && *def == ';')
            def++;
        else
            break;
    }
    return found == 1;
}

bool mtd_offset_from_log(const char *log, size_t len, const char *name,
                         uint64_t size, uint64_t *offset) {
    int found = 0;
    const char *end = log + len;
    while (log < end) {
        const char *nl = memchr(log, '\n', end - log);
        size_t n = (nl ? nl : end) - log;
        char line[256], part[128];
        unsigned long long from, to;
        if (n < sizeof(line)) {
            memcpy(line, log, n);
            line[n] = 0;
            // Behind a <5> level and a timestamp, or neither
            const char *p = strstr(line, "0x");
            int end_of_line = -1;
            if (p)
                sscanf(p, "0x%llx-0x%llx : \"%127[^\"]\"%n", &from, &to, part,
                       &end_of_line);
            // %n is only reached past the closing quote: a line the ring
            // buffer tore in two has a shorter name in it
            if (end_of_line >= 0 && !strcmp(part, name) && to - from == size) {
                if (found && *offset != from)
                    found = 2;
                else if (!found)
                    found = 1;
                *offset = from;
            }
        }
        log += n + 1;
    }
    return found == 1;
}
