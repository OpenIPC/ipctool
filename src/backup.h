#ifndef BACKUP_H
#define BACKUP_H

bool udp_lock();
int do_backup(const char *yaml, size_t yaml_len, bool wait_mode,
              const char *filename);
int upgrade_restore_cmd(int argc, char **argv);

#endif /* BACKUP_H */
