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
 * One row of the running SoC's pad-mux table: a physical register, the
 * selector value that puts one named function on that pad, and the GPIO the
 * pad carries the rest of the time.
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
 * Only HiSilicon and Goke parts carry real alternatives per pad. The
 * SigmaStar and Ingenic tables name one function per register, so on those
 * `gpio_name` is NULL, `gpio_pad` is -1, and a lookup by pad finds nothing.
 * ---------------------------------------------------------------------- */
typedef struct {
    uint32_t address;      /* pad-mux register, physical address */
    uint32_t func_mask;    /* the selector field, based at bit 0: write
                            * (old & ~func_mask) | func */
    int func;              /* selector value that puts func_name on the pad */
    const char *func_name; /* the table's spelling, e.g. "PWM1" */
    const char *gpio_name; /* "GPIO0_4", or NULL if this pad has no GPIO */
    int gpio_pad;          /* gpio_name as bank*8+pin, or -1 */
    int gpio_func;         /* selector value that restores the GPIO, or -1 */
} ipchw_padmux_t;

enum {
    IPCHW_PADMUX_NO_CHIP = -1,  /* SoC detection failed */
    IPCHW_PADMUX_NO_TABLE = -2, /* no table for this SoC in this build */
    IPCHW_PADMUX_BAD_ARG = -3,
};

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
 * Returns the total number of matches, which may be larger than `max`; at
 * most `max` rows are written, so a caller can size a second call from the
 * first. Negative values are IPCHW_PADMUX_*. "reserved" never matches. */
int ipchw_padmux_by_func(const char *func_name, ipchw_padmux_t *out, int max);

/* As above, but every function whose name starts with `prefix`. An empty
 * prefix matches every function on every pad, which is how you walk the whole
 * table. */
int ipchw_padmux_by_prefix(const char *prefix, ipchw_padmux_t *out, int max);

/* Every alternative function of one pad, `pad` being bank*8+pin. The row
 * whose func equals gpio_func is the plain-GPIO one and is included. */
int ipchw_padmux_by_pad(int pad, ipchw_padmux_t *out, int max);

#endif /* IPCHW_H */
