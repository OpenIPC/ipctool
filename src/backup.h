#ifndef BACKUP_H
#define BACKUP_H

bool udp_lock();
void do_backup(const char *yaml, size_t yaml_len);

#endif /* BACKUP_H */
