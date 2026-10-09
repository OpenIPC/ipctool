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
