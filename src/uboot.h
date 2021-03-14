#ifndef UBOOT_H
#define UBOOT_H

#include <stdlib.h>

int uboot_detect_env(void *buf, size_t len);
const char *uboot_getenv(const char *name);
void uboot_freeenv();
void uboot_copyenv(void *buf);

#endif /* UBOOT_H */
