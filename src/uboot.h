#ifndef UBOOT_H
#define UBOOT_H

#include <stdlib.h>

int uboot_detect_env(void *buf, size_t len);
const char *uboot_getenv(const char *name);
void uboot_freeenv();
void uboot_copyenv(const void *buf);
void printenv();
void set_env(const char *option);

#endif /* UBOOT_H */
