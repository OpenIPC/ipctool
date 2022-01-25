#ifndef UBOOT_H
#define UBOOT_H

#define ENV_MTD_NUM 2

#include <stdlib.h>

enum FLASH_OP {
    FOP_INTERACTIVE,
    FOP_RAM,
    FOP_ROM,
};

int uboot_detect_env(void *buf, size_t size, size_t erasesize);
const char *uboot_getenv(const char *name);
void uboot_copyenv_int(const void *buf);
void set_env_param(const char *key, const char *value, enum FLASH_OP op);
const char *uboot_env_findnsave();

int cmd_printenv();
int cmd_set_env(int argc, char **argv);

#endif /* UBOOT_H */
