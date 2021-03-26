#ifndef BACKUP_H
#define BACKUP_H

bool udp_lock();
int do_backup(const char *yaml, size_t yaml_len, bool wait_mode);
int restore_backup();

#endif /* BACKUP_H */
