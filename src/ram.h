#ifndef RAM_H
#define RAM_H

#include <cjson/cJSON.h>

void linux_mem();
unsigned long kernel_mem();
cJSON *detect_ram();

#endif /* RAM_H */
