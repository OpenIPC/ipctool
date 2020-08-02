#ifndef HAL_HISI_H
#define HAL_HISI_H

#include <stdbool.h>
#include <sys/types.h>

#define VENDOR_HISI "HiSilicon"

int hisi_SYS_DRV_GetChipId();
const char *hisi_cv200_get_sensor_clock();
const char *hisi_cv200_get_sensor_data_type();
const char *hisi_cv300_get_sensor_clock();
const char *hisi_cv300_get_sensor_data_type();
const char *hisi_ev300_get_sensor_clock();
const char *hisi_ev300_get_sensor_data_type();
bool hisi_ev300_get_die_id(char *buf, ssize_t len);
int hisi_get_temp();

#endif /* HAL_HISI_H */
