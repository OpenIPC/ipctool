#pragma once

extern char system_id[128];
extern char chip_id[128];
extern int  isp_register;
extern char isp_version[128];
extern char isp_build_number[128];
extern char isp_sequence_number[128];
extern char mpp_version[128];

int get_chip_id();
int get_isp_version();
int get_mpp_version();
