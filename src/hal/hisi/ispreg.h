#ifndef HISI_ISPREG_H
#define HISI_ISPREG_H

#include <stdbool.h>
#include <stdint.h>

#define CV300_CRG_BASE 0x12010000
#define CV300_PERI_CRG11_ADDR CV300_CRG_BASE + 0x002c

struct EV300_PERI_CRG60 {
    bool sensor0_cken : 1;
    unsigned int sensor0_srst_req : 1;
    unsigned int sensor0_cksel : 3;
    bool sensor0_ctrl_cken : 1;
    unsigned int sensor0_ctrl_srst_req : 1;
};

#define EV300_PERI_CRG60_ADDR 0x120100F0

struct CV610_PERI_CRG8464 {
    unsigned int sensor0_srst_req : 1;        /* [0]   Soft reset request */
    unsigned int sensor0_ctrl_srst_req : 1;   /* [1]   Soft reset request of slave mode control */
    unsigned int reserved_3_2 : 2;            /* [3:2] Reserved */
    unsigned int sensor0_cken : 1;            /* [4]   Clock gating (0: disabled, 1: enabled) */
    unsigned int reserved_11_5 : 7;           /* [11:5] Reserved */
    unsigned int sensor0_cksel : 4;           /* [15:12] Clock select */
    unsigned int clk_sensor0_pctrl : 1;       /* [16]  Clock phase control (0: non inverted, 1: inverted) */
    unsigned int reserved_31_17 : 15;         /* [31:17] Reserved */
};

#define CV610_PERI_CRG8464_ADDR 0x11018440

const char *hisi_detect_fmc();
void hisi_chip_properties(cJSON *j_inner);

#endif /* HISI_ISPREG_H */
