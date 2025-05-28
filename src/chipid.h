#ifndef CHIPID_H
#define CHIPID_H

#include <stdbool.h>

#define VENDOR_ALLWINNER "Allwinner"
#define VENDOR_BCM "Broadcom"
#define VENDOR_FH "Fullhan"
#define VENDOR_GM "GrainMedia"
#define VENDOR_GOKE "Goke"
#define VENDOR_HISI "HiSilicon"
#define VENDOR_INGENIC "Ingenic"
#define VENDOR_NOVATEK "Novatek"
#define VENDOR_ROCKCHIP "Rockchip"
#define VENDOR_SSTAR "SigmaStar"

extern int chip_generation;
extern char chip_name[128];
extern char control[128];
extern char sensor_id[128];
extern char nor_chip_name[128];
extern char nor_chip_id[128];

const char *getchipname();

#ifndef STANDALONE_LIBRARY
#include "cjson/cJSON.h"

cJSON *detect_chip();
#endif

#endif /* CHIPID_H */
