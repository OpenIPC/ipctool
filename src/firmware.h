#ifndef FIRMWARE_H
#define FIRMWARE_H

#include <stdbool.h>

cJSON *detect_firmare();
pid_t get_god_pid(char *shortname, size_t shortsz);

#endif /* FIRMWARE_H */
