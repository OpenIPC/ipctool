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

/* The same two fields as bit masks, for the save/restore in hal_hisi.c.
 *
 * That code works on the raw word rather than the struct above: the struct
 * names 7 of 32 bits, so a struct assignment carries 25 bits C does not
 * promise to copy, and "is this register still exactly what I wrote" has to
 * be asked of the whole word to mean anything. */
#define EV300_PERI_CRG60_SENSOR0_CKEN 0x1u
#define EV300_PERI_CRG60_SENSOR0_SRST 0x2u

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
#define CV610_PERI_CRG8472_ADDR 0x11018460

/* And the same for OT, where the two fields sit in different places. */
#define CV610_PERI_CRG_SENSOR0_SRST 0x1u
#define CV610_PERI_CRG_SENSOR0_CKEN 0x10u
/* sensorN_cksel [15:12]. The reset value 0 is 74.25 MHz on both CV610 and
 * DV500 (data sheets, PERI_CRG8464/8472), above the 27 MHz an OS08A10/A20 or
 * the 36 MHz an OS04A10 accepts; 4 is 24 MHz, which every sensor we probe
 * takes. */
#define CV610_PERI_CRG_SENSOR_CKSEL_MASK 0xF000u
#define CV610_PERI_CRG_SENSOR_CKSEL_24M 0x4000u

const char *hisi_detect_fmc();
void hisi_chip_properties(cJSON *j_inner);

#endif /* HISI_ISPREG_H */
