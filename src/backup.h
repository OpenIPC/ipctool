#ifndef BACKUP_H
#define BACKUP_H

int do_backup(const char *yaml, size_t yaml_len, const char *filename);
int upgrade_restore_cmd(int argc, char **argv);

#endif /* BACKUP_H */
