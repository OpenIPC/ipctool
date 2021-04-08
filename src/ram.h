#ifndef RAM_H
#define RAM_H

#include <cjson/cJSON.h>

void linux_mem();
unsigned long kernel_mem();
cJSON *detect_ram();
void hal_ram(unsigned long *media_mem, uint32_t *total_mem);
uint32_t rounded_num(uint32_t n);

#endif /* RAM_H */
