#ifndef BACKUP_H
#define BACKUP_H

#include <stddef.h>

#include "dns.h"
#include "http.h"

int do_backup(const char *yaml, size_t yaml_len, const char *filename);
/* blocks[0] is the YAML with its NUL, then every MTD partition (or UBI
 * volume) mapped read-only: what `backup` writes and `upload --backup` sends.
 * Returns how many blocks were filled; *missed counts the partitions or
 * volumes that could not be read and are absent. */
size_t backup_blocks(const char *yaml, size_t yaml_len, span_t *blocks,
                     size_t *missed);
int upgrade_restore_cmd(int argc, char **argv);

#endif /* BACKUP_H */
