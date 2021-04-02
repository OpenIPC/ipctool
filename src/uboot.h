#ifndef UBOOT_H
#define UBOOT_H

#define ENV_MTD_NUM 2

#include <stdlib.h>

int uboot_detect_env(void *buf, size_t len);
const char *uboot_getenv(const char *name);
void uboot_freeenv();
void uboot_copyenv(const void *buf);
void printenv();
void cmd_set_env(char *option);
void set_env_param(const char *key, const char *value, bool to_flash);

#endif /* UBOOT_H */
