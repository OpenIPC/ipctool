#ifndef UBOOT_H
#define UBOOT_H

#define ENV_MTD_NUM 2

#include <stdlib.h>

int uboot_detect_env(void *buf, size_t size, size_t erasesize);
const char *uboot_env_get_param(const char *name);
void uboot_copyenv_int(const void *buf);
char *uboot_fullenv(size_t *len);

void set_env_param_ram(const char *key, const char *value);
void set_env_param_rom(const char *key, const char *value, int i, size_t u_off,
                       size_t erasesize);

int cmd_printenv();
int cmd_set_env(int argc, char **argv);

#endif /* UBOOT_H */
