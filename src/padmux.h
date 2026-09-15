#ifndef PADMUX_H
#define PADMUX_H

#include <stdbool.h>
#include <stdint.h>

#include "ipchw.h"

/* ------------------------------------------------------------------------
 * The inside of the pad-mux API. include/ipchw.h has the contract a consumer
 * sees; this is the seam the three vendors meet at.
 *
 * They select a pad's function three different ways, and only the first of
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
 *
 * What a caller sees is a flat ipchw_padmux_t in every case.
 * ---------------------------------------------------------------------- */

/* Which functions of a row a walk wants. NULL means every one of them. */
typedef bool (*padmux_match_fn)(const char *func_name, const void *arg);

typedef struct {
    const char *name; /* "hisi", "sstar", "ingenic" */

    /* The same counting contract as the public lookups: the return value is
     * the number of MATCHES, which may exceed `max`; at most `max` rows are
     * written. `pad` of -1 means every pad. Touches no registers. */
    int (*walk)(padmux_match_fn match, const void *arg, int pad,
                ipchw_padmux_t *out, int max);
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

/* The backend for the SoC that has been detected, or NULL when this build
 * carries no pad-mux table that could serve it. */
const padmux_ops_t *padmux_ops(void);

#endif /* PADMUX_H */
