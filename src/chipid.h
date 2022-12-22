#ifndef CHIPID_H
#define CHIPID_H

#include <stdbool.h>

extern int chip_generation;
extern char chip_name[128];
extern char control[128];
extern char sensor_id[128];
extern char nor_chip[128];

const char *getchipname();

#ifndef STANDALONE_LIBRARY
#include "cjson/cJSON.h"

cJSON *detect_chip();
#endif

#endif /* CHIPID_H */
