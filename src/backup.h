#ifndef BACKUP_H
#define BACKUP_H

bool udp_lock();
int do_backup(const char *yaml, size_t yaml_len, bool wait_mode,
              const char *filename);
int restore_backup(const char *arg, bool skip_env, bool force);
int do_upgrade(const char *filename, bool force);

#endif /* BACKUP_H */
