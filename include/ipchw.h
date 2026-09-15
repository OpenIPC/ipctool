#ifndef IPCHW_H
#define IPCHW_H

#include <stdint.h>

const char *getchipname();
const char *getchipfamily();
const char* getchipvendor();
const char *getsensoridentity();
const char *getsensorshort();
float gethwtemp();

/* ------------------------------------------------------------------------
 * Pad multiplexing
 *
 * One row of the running SoC's pad-mux table: a named function, the pad it
 * lands on, and -- where one register write is the whole story -- the register
 * and the value that put it there.
 *
 * `func_name` and `gpio_name` point into this library's read-only data. They
 * live for the process, are never freed, and are unaffected by later calls --
 * but compare them with strcmp(), not by pointer.
 *
 * Threading: the lookups walk constant data and take no locks, so they are
 * safe to call concurrently -- but the first call detects the SoC, which is
 * not. Call getchipname() once from a single thread at startup (or set
 * chip_generation yourself) before using these from more than one.
 *
 * They never exit, never print and never touch /dev/mem. Acting on what they
 * return does: mem_reg() keeps one unsynchronised mmap window, so every
 * register write must come from a single thread.
 *
 * Three vendors select a pad's function three different ways. HiSilicon and
 * Goke put a selector field in the pad's own register. SigmaStar puts a field
 * per PERIPHERAL, whose value picks which group of pads carries it, so one
 * pad's alternatives are fields in different registers and the pad is plain
 * GPIO when none of them claims it. Ingenic spreads four bits across four
 * port registers at the pin's own bit position. The rows below say which of
 * those you are holding: see IPCHW_PADMUX_F_RMW.
 * ---------------------------------------------------------------------- */
typedef struct {
    uint32_t address;      /* pad-mux register, physical address, or
                            * IPCHW_PADMUX_ADDR_NONE -- see the flags */
    uint32_t func_mask;    /* the selector field, IN PLACE: write
                            * (old & ~func_mask) | func */
    int func;              /* selector value, in place, or -1 */
    const char *func_name; /* the table's spelling, e.g. "PWM1" */
    const char *gpio_name; /* the vendor's name for the PAD: "GPIO0_4" on
                            * HiSilicon, "PAD_SR_IO03" on SigmaStar, "PB25"
                            * on Ingenic. NULL if the pad has no GPIO. */
    int gpio_pad;          /* that pad as the kernel numbers it -- bank*8+pin
                            * on HiSilicon, the pad id on SigmaStar, port*32+
                            * pin on Ingenic -- or -1 */
    int gpio_func;         /* selector value that restores the GPIO, or -1
                            * where restoring it is not one write */
    uint32_t flags;        /* IPCHW_PADMUX_F_* */
} ipchw_padmux_t;

enum {
    IPCHW_PADMUX_NO_CHIP = -1,  /* SoC detection failed */
    IPCHW_PADMUX_NO_TABLE = -2, /* no table for this SoC in this build */
    IPCHW_PADMUX_BAD_ARG = -3,
};

enum {
    /* `(old & ~func_mask) | func` at `address` is the WHOLE operation that
     * puts func_name on this pad. Clear means it is not -- call
     * ipchw_padmux_set() instead. `address` is then IPCHW_PADMUX_ADDR_NONE,
     * `func_mask` is 0 and `func` is -1, so a read-modify-write composed from
     * such a row writes the register back unchanged rather than aiming
     * somewhere wrong. */
    IPCHW_PADMUX_F_RMW = 1u << 0,
    /* Every row of this pad shares `address` and `func_mask`, so one read of
     * that register tells you which of them is live. Clear means the
     * alternatives live in different registers: ask ipchw_padmux_get(). */
    IPCHW_PADMUX_F_SHARED_REG = 1u << 1,
    /* This row is the pad's plain-GPIO alternative. Test the flag rather than
     * the spelling: func_name is "GPIO5_2" where GPIO is a selector value and
     * IPCHW_PADMUX_GPIO where it is the absence of every claim. */
    IPCHW_PADMUX_F_GPIO = 1u << 2,
};

/* `address` of a row whose function is not one register write. Reads of it
 * yield zero and writes to it do nothing, by design. */
#define IPCHW_PADMUX_ADDR_NONE 0xdeadbeefu

/* Every pad that can carry pin-mux function `func_name`, exactly spelled.
 *
 * A function is NOT unique to a pad: hi3516ev300 offers PWM2 and PWM3 on
 * three pads each, and hi3516cv200 offers PWM0 on two. A caller that wants a
 * particular wire has to look at every row.
 *
 * The spelling is not portable across families either -- PWM_OUT0 on the V1
 * parts, PWM0 from V2 on, PWM0_OUT1 on V5 -- so ipchw_padmux_by_prefix() is
 * usually the query you want. Beware that "PWM" also prefixes SVB_PWM and
 * PMC_PWM, which are different controllers; match anchored, not anywhere.
 *
 * It is not portable across VENDORS at all. SigmaStar names a bus and not a
 * wire -- "I2C1_MODE_3" claims two pads and does not say which is SCL -- and
 * Ingenic uses the datasheet's own lower-case-derived spelling, SMB for I2C
 * and SSI for SPI. Where the function name does not say which line a pad
 * carries, `gpio_name` often does: PAD_I2C1_SCL, PAD_SPI0_DI, PAD_UART1_RX.
 *
 * Returns the total number of matches, which may be larger than `max`; at
 * most `max` rows are written, so a caller can size a second call from the
 * first. Negative values are IPCHW_PADMUX_*. "reserved" never matches. */
int ipchw_padmux_by_func(const char *func_name, ipchw_padmux_t *out, int max);

/* As above, but every function whose name starts with `prefix`. An empty
 * prefix matches every function on every pad, which is how you walk the whole
 * table. */
int ipchw_padmux_by_prefix(const char *prefix, ipchw_padmux_t *out, int max);

/* Every alternative function of one pad, `pad` as the kernel numbers it. The
 * row flagged IPCHW_PADMUX_F_GPIO is the plain-GPIO one and is included. The
 * order is the table's own and is not a promise; test the flag. */
int ipchw_padmux_by_pad(int pad, ipchw_padmux_t *out, int max);

#endif /* IPCHW_H */
