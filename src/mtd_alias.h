#ifndef MTD_ALIAS_H
#define MTD_ALIAS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Marks alias[i] for each partition that is another name for flash the
 * others already cover -- Novatek's "all", 0..chip size, after "loader" to
 * "encrypt". Where the kernel gives offsets (has_off), a partition that
 * contains another one whole is the alias. Without them, one whose size is
 * the sum of all the others' is. Backing it up would put the chip in twice. */
void mtd_mark_aliases(int n, const uint64_t *off, const bool *has_off,
                      const uint64_t *size, bool *alias);

/*
 * Where a partition starts, for kernels before 4.4: they have no
 * /sys/class/mtd/mtdN/offset. A partition is found by name and size, and is
 * not found if two places claim it.
 *
 * From the `mtdparts=` on a kernel command line. Only says something when the
 * kernel took its partitions from there, which the size match stands in for.
 */
bool mtd_offset_from_cmdline(const char *cmdline, const char *name,
                             uint64_t size, uint64_t *offset);
/* From the line the kernel logs per partition as it adds it, which also covers
 * tables built into the kernel. `log` need not be NUL-terminated. */
bool mtd_offset_from_log(const char *log, size_t len, const char *name,
                         uint64_t size, uint64_t *offset);

#endif /* MTD_ALIAS_H */
