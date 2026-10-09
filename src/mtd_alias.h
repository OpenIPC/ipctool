#ifndef MTD_ALIAS_H
#define MTD_ALIAS_H

#include <stdbool.h>
#include <stdint.h>

/* Marks alias[i] for each partition that is another name for flash the
 * others already cover -- Novatek's "all", 0..chip size, after "loader" to
 * "encrypt". Where the kernel gives offsets (has_off), a partition that
 * contains another one whole is the alias. Without them, one whose size is
 * the sum of all the others' is. Backing it up would put the chip in twice. */
void mtd_mark_aliases(int n, const uint64_t *off, const bool *has_off,
                      const uint64_t *size, bool *alias);

#endif /* MTD_ALIAS_H */
