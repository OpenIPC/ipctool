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
 * The lookups never exit, never print and never touch /dev/mem.
 * ipchw_padmux_get() and ipchw_padmux_set() do: they reach physical registers
 * through one process-wide mmap window that is not locked, is not thread safe
 * and is shared with every other register access in this library. Call them
 * from one thread, and serialise with them if you reach registers yourself.
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
                            * where restoring it is not one write. See
                            * ipchw_padmux_get(), which can fill this in on a
                            * family where the lookups cannot. */
    uint32_t flags;        /* IPCHW_PADMUX_F_* */
} ipchw_padmux_t;

enum {
    IPCHW_PADMUX_NO_CHIP = -1,  /* SoC detection failed */
    IPCHW_PADMUX_NO_TABLE = -2, /* no table for this SoC in this build */
    IPCHW_PADMUX_BAD_ARG = -3,
    IPCHW_PADMUX_NO_PAD = -4,  /* this SoC has no pad with that number */
    IPCHW_PADMUX_NO_FUNC = -5, /* that pad cannot carry that function */
    IPCHW_PADMUX_IO = -6,      /* a register could not be reached */
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

/* The function name that means "no peripheral", accepted by
 * ipchw_padmux_set() on every family. */
#define IPCHW_PADMUX_GPIO "GPIO"

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

/* What pad `pad` is carrying right now.
 *
 * 1 and *out filled. 0 when the pad exists but its selector holds a value
 * this build has no name for -- a hole in the table, or a mode newer than it;
 * *out is untouched. Negative values are IPCHW_PADMUX_*.
 *
 * A pad carrying no peripheral answers with its plain-GPIO row, flagged
 * IPCHW_PADMUX_F_GPIO.
 *
 * This is also the call that can answer "and how do I put it back". Where
 * plain GPIO is the absence of every claim rather than a selector value, the
 * lookups above have to leave gpio_func at -1, because which value restores
 * it depends on what the pad is carrying and the table does not know. This
 * one has just read the registers: when a single field is claiming the pad
 * and clearing it is the whole job, the row comes back with gpio_func set, so
 * (address, func_mask, gpio_func) is one write like anywhere else. When it
 * takes more than one write gpio_func stays -1 and ipchw_padmux_set(pad,
 * IPCHW_PADMUX_GPIO) is the way.
 *
 * READS /dev/mem; see the threading note above. */
int ipchw_padmux_get(int pad, ipchw_padmux_t *out);

/* Put `func_name` on `pad`, spelled exactly as the lookups return it.
 *
 * IPCHW_PADMUX_GPIO means "no peripheral" and is accepted on every family: it
 * writes the GPIO selector where there is one and clears every claim on the
 * pad where there is not. It muxes the pad; it does not drive it, and it does
 * not change the direction the pad already had.
 *
 * 0 on success. IPCHW_PADMUX_NO_FUNC when this pad cannot carry that
 * function, and nothing was written. IPCHW_PADMUX_IO when a register could
 * not be reached, in which case a pad that needs more than one write may be
 * left half set -- the operation is idempotent, so call it again.
 *
 * WRITES /dev/mem; see the threading note above. */
int ipchw_padmux_set(int pad, const char *func_name);

#endif /* IPCHW_H */
