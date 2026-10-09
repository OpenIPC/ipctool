#ifndef PADMUX_H
#define PADMUX_H

#include <stdbool.h>
#include <stdint.h>

#include "ipchw.h"

/* ------------------------------------------------------------------------
 * The inside of the pad-mux API. include/ipchw.h has the contract a consumer
 * sees; this is the seam the three vendors meet at.
 *
 * They select a pad's function four different ways, and only the first of
 * them is one register per pad:
 *
 *   HiSilicon/Goke  one register per pad, a selector nibble indexing the list
 *                   of functions. That is muxctrl_reg_t in reginfo.h.
 *   SigmaStar       one register field per PERIPHERAL, whose value picks which
 *                   group of pads carries it. One pad's alternatives are
 *                   fields in different registers, and the pad is GPIO when
 *                   none of them claims it -- so there is no list to index.
 *   Ingenic         four bits at the pin's own position in four different
 *                   registers (INT, MSK, PAT1, PAT0), so no single write
 *                   selects a function at all.
 *   Novatek         SigmaStar's per-peripheral fields, plus a gate bit per
 *                   pad that has to be cleared before any of them reaches
 *                   it. One pad's function is a field in one register and a
 *                   bit in another.
 *
 * What a caller sees is a flat ipchw_padmux_t in every case.
 * ---------------------------------------------------------------------- */

/* How a backend reaches a register.
 *
 * Indirected for one reason: reginfo_test drives get() and set() against a
 * fabricated register file on a host with no /dev/mem, which is the only way
 * the write paths get exercised at all until someone runs this on a camera.
 *
 * `width` is 16 or 32. SigmaStar's RIU ports are half-word registers in a
 * four-byte slot whose upper half is not mapped, so a 32-bit store there
 * writes two bytes that do not exist. */
typedef struct {
    bool (*read)(uint32_t addr, uint32_t *val, int width);
    bool (*write)(uint32_t addr, uint32_t val, int width);
} padmux_io_t;

/* Install a different accessor, or NULL to go back to mem_reg(). Returns the
 * one that was in place. Deliberately not in include/ipchw.h: it is a test
 * seam, not API. */
const padmux_io_t *padmux_set_io(const padmux_io_t *io);

/* Which functions of a row a walk wants. NULL means every one of them. */
typedef bool (*padmux_match_fn)(const char *func_name, const void *arg);

typedef struct {
    const char *name; /* "hisi", "sstar", "ingenic", "novatek" */

    /* The same counting contract as the public lookups: the return value is
     * the number of MATCHES, which may exceed `max`; at most `max` rows are
     * written. `pad` of -1 means every pad. Touches no registers. */
    int (*walk)(padmux_match_fn match, const void *arg, int pad,
                ipchw_padmux_t *out, int max);

    /* What `pad` carries right now: 1 and *out filled, 0 when the selector
     * holds a value this table has no name for, or IPCHW_PADMUX_*. */
    int (*get)(int pad, ipchw_padmux_t *out, const padmux_io_t *io);

    /* Put `func_name` on `pad`: 0, or IPCHW_PADMUX_*. */
    int (*set)(int pad, const char *func_name, const padmux_io_t *io);

    /* The vendor's own spellings of a pad, to its number, or -1. NULL where
     * the two generic ones in padmux_parse_pad() are all there is. */
    int (*parse_pad)(const char *spec);
} padmux_ops_t;

/* Always present: reginfo.c is compiled into every configuration, and its
 * walk answers IPCHW_PADMUX_NO_TABLE for a build whose tables were all
 * trimmed out. */
extern const padmux_ops_t PADMUX_OPS_HISI;
#ifdef IPCHW_PADMUX_SSTAR
extern const padmux_ops_t PADMUX_OPS_SSTAR;
#endif
#ifdef IPCHW_PADMUX_INGENIC
extern const padmux_ops_t PADMUX_OPS_INGENIC;
#endif
#ifdef IPCHW_PADMUX_NOVATEK
extern const padmux_ops_t PADMUX_OPS_NOVATEK;
#endif

/* The backend for the SoC that has been detected, or NULL when this build
 * carries no pad-mux table that could serve it. */
const padmux_ops_t *padmux_ops(void);

/* True when `pad` is carrying no peripheral right now. Reads registers.
 * False for a pad this build cannot ask about, which is what every caller
 * wants: "not known to be free" rather than "free". */
bool padmux_pad_is_gpio(int pad);

/* "5_2" or "42" -> 42, the two spellings every gpio subcommand accepts, or
 * the detected SoC's own: "P_GPIO22" on Novatek, where "5_2" is refused
 * because the bank-of-eight numbering is HiSilicon's. -1 for anything else. */
int padmux_parse_pad(const char *spec);

#endif /* PADMUX_H */
