#ifndef BACKUP_H
#define BACKUP_H

#include <stddef.h>

#include "dns.h"
#include "http.h"

int do_backup(const char *yaml, size_t yaml_len, const char *filename);
/* blocks[0] is the YAML with its NUL, then every MTD partition (or UBI
 * volume) mapped read-only: what `backup` writes and `upload --backup` sends.
 * Returns how many blocks were filled. */
size_t backup_blocks(const char *yaml, size_t yaml_len, span_t *blocks);
int upgrade_restore_cmd(int argc, char **argv);

#endif /* BACKUP_H */
