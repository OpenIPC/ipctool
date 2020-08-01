#ifndef HAL_HISI_H
#define HAL_HISI_H

#define VENDOR_HISI "HiSilicon"

int hisi_SYS_DRV_GetChipId();
const char *hisi_cv300_get_sensor_clock();
const char *hisi_cv300_get_sensor_data_type();
int hisi_get_temp();

#endif /* HAL_HISI_H */
