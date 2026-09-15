/* The pad-mux API's front door: resolve the SoC, pick its backend, ask.
 *
 * The contract every function here implements is on the prototypes in
 * include/ipchw.h; the seam the backends plug into is src/padmux.h. Nothing
 * vendor-specific lives in this file, and nothing here knows what a register
 * looks like.
 *
 * Compiled unconditionally. A build whose tables were all trimmed out still
 * links: the backend it lands on answers IPCHW_PADMUX_NO_TABLE, which is the
 * honest answer and the same one an unknown SoC gets. */

#include "padmux.h"

#include "chipid.h"
#include "hal/ingenic.h"
#include "hal/sstar.h"
#include "tools.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------
 * Register access
 * ---------------------------------------------------------------------- */

static bool io_mem_read(uint32_t addr, uint32_t *val, int width) {
    return mem_reg(addr, val, width == 16 ? OP_READ_16 : OP_READ);
}

static bool io_mem_write(uint32_t addr, uint32_t val, int width) {
    return mem_reg(addr, &val, width == 16 ? OP_WRITE_16 : OP_WRITE);
}

static const padmux_io_t MEM_IO = {io_mem_read, io_mem_write};
static const padmux_io_t *IO = &MEM_IO;

const padmux_io_t *padmux_set_io(const padmux_io_t *io) {
    const padmux_io_t *was = IO;

    IO = io ? io : &MEM_IO;
    return was;
}

/* ------------------------------------------------------------------------
 * Which backend
 * ---------------------------------------------------------------------- */

const padmux_ops_t *padmux_ops(void) {
    switch (chip_generation) {
#ifdef IPCHW_PADMUX_SSTAR
    case INFINITY6:
    case INFINITY6B:
    case INFINITY6C:
    case INFINITY6E:
        return &PADMUX_OPS_SSTAR;
#endif
#ifdef IPCHW_PADMUX_INGENIC
    case T31:
        return &PADMUX_OPS_INGENIC;
#endif
    default:
        break;
    }

    /* Everything else is a pad register with a selector nibble, or it is a
     * SoC nobody has entered a table for -- which the HiSilicon backend
     * reports as IPCHW_PADMUX_NO_TABLE, the same answer a trimmed build
     * gives. A caller cannot tell those apart and does not need to. */
    return &PADMUX_OPS_HISI;
}

/* The SoC has to be known before any of this means anything. Detection is the
 * one part that is not safe to do concurrently, which is why the header asks
 * for a getchipname() at startup. */
static int resolve(const padmux_ops_t **ops) {
    if (!chip_generation && getchipname() == NULL)
        return IPCHW_PADMUX_NO_CHIP;
    if (!chip_generation)
        return IPCHW_PADMUX_NO_CHIP;

    const padmux_ops_t *o = padmux_ops();
    if (o == NULL)
        return IPCHW_PADMUX_NO_TABLE;

    *ops = o;
    return 0;
}

/* ------------------------------------------------------------------------
 * The lookups
 * ---------------------------------------------------------------------- */

static bool match_exact(const char *func_name, const void *arg) {
    return !strcmp(func_name, (const char *)arg);
}

static bool match_prefix(const char *func_name, const void *arg) {
    const char *prefix = arg;

    return !strncmp(func_name, prefix, strlen(prefix));
}

static int lookup(padmux_match_fn match, const void *arg, int pad,
                  ipchw_padmux_t *out, int max) {
    if (max < 0 || (max > 0 && out == NULL))
        return IPCHW_PADMUX_BAD_ARG;

    const padmux_ops_t *ops;
    int res = resolve(&ops);
    if (res < 0)
        return res;

    return ops->walk(match, arg, pad, out, max);
}

int ipchw_padmux_by_func(const char *func_name, ipchw_padmux_t *out, int max) {
    if (func_name == NULL || !*func_name)
        return IPCHW_PADMUX_BAD_ARG;

    return lookup(match_exact, func_name, -1, out, max);
}

int ipchw_padmux_by_prefix(const char *prefix, ipchw_padmux_t *out, int max) {
    if (prefix == NULL)
        return IPCHW_PADMUX_BAD_ARG;

    /* An empty prefix is every function, which is how a caller walks the
     * whole table -- an integrity sweep, or a "what can this pad do" report. */
    return lookup(match_prefix, prefix, -1, out, max);
}

int ipchw_padmux_by_pad(int pad, ipchw_padmux_t *out, int max) {
    if (pad < 0)
        return IPCHW_PADMUX_BAD_ARG;

    return lookup(NULL, NULL, pad, out, max);
}

/* ------------------------------------------------------------------------
 * The accessors
 * ---------------------------------------------------------------------- */

int ipchw_padmux_get(int pad, ipchw_padmux_t *out) {
    if (pad < 0 || out == NULL)
        return IPCHW_PADMUX_BAD_ARG;

    const padmux_ops_t *ops;
    int res = resolve(&ops);
    if (res < 0)
        return res;

    return ops->get(pad, out, IO);
}

int ipchw_padmux_set(int pad, const char *func_name) {
    if (pad < 0 || func_name == NULL || !*func_name)
        return IPCHW_PADMUX_BAD_ARG;

    const padmux_ops_t *ops;
    int res = resolve(&ops);
    if (res < 0)
        return res;

    return ops->set(pad, func_name, IO);
}

bool padmux_pad_is_gpio(int pad) {
    ipchw_padmux_t row;

    return ipchw_padmux_get(pad, &row) == 1 &&
           (row.flags & IPCHW_PADMUX_F_GPIO) != 0;
}

/* Both spellings every gpio subcommand has ever taken. "5_2" is the one the
 * HiSilicon tables use; the plain running number is what the rest of the
 * world, and every other vendor here, speaks. */
int padmux_parse_pad(const char *spec) {
    if (spec == NULL || !*spec)
        return -1;

    int bank, pin, end = 0;
    if (sscanf(spec, "%d_%d%n", &bank, &pin, &end) == 2) {
        /* %n has to land on the terminator. Without it "5_2junk" and "1_2_3"
         * both parse as a valid pad, and the caller writes a register on the
         * strength of a typo. */
        if (spec[end] != '\0')
            return -1;
        if (bank < 0 || pin < 0 || pin > 7)
            return -1;
        return bank * 8 + pin;
    }

    char *tail;
    long n = strtol(spec, &tail, 10);
    if (tail == spec || *tail != '\0' || n < 0 || n > 0xffff)
        return -1;

    return (int)n;
}
