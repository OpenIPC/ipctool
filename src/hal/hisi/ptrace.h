#ifndef HISI_PTRACE_H
#define HISI_PTRACE_H

#include <sys/types.h>

#define HIV2X_MIPI_SET_DEV_ATTR 0X41306D01
#define HIV3A_MIPI_SET_DEV_ATTR 0X41D86D01
#define HIV3_HI_MIPI_SET_DEV_ATTR 0X40BC6D01
#define HIV4A_MIPI_SET_DEV_ATTR 0X40C86D01
#define HIV4_MIPI_SET_DEV_ATTR 0X40886D01

#define HIV4_VI_SET_DEV_ATTR 0x40904900

#define HI_MIPI_RESET_MIPI 0X40046D07
#define HI_MIPI_RESET_SENSOR 0X40046D05
#define HI_MIPI_UNRESET_MIPI 0X40046D08
#define HI_MIPI_UNRESET_SENSOR 0X40046D06
#define HI_MIPI_SET_HS_MODE 0X40046D0B
#define HI_MIPI_ENABLE_MIPI_CLOCK 0X40046D0C
#define HI_MIPI_ENABLE_SENSOR_CLOCK 0X40046D10

size_t hisi_sizeof_combo_dev_attr();
size_t hisi_sizeof_vi_dev_attr();
void hisi_dump_combo_dev_attr(void *ptr, unsigned int cmd);
void hisi_dump_vi_dev_attr(void *ptr, unsigned int cmd);

#endif /* HISI_PTRACE_H */
