/* SPDX-License-Identifier: MIT */
#ifndef NT98513_SENSOR_CLI_H
#define NT98513_SENSOR_CLI_H
#include "nt98513_sensor_query.h"
enum nt98513_sensor_cli_exit {
    NT98513_SENSOR_CLI_OK = 0,
    NT98513_SENSOR_CLI_NOT_SELECTED = 1,
    NT98513_SENSOR_CLI_USAGE = 2,
    NT98513_SENSOR_CLI_CLIENT_ERROR = 3,
    NT98513_SENSOR_CLI_REPLY_ERROR = 4,
    NT98513_SENSOR_CLI_WRITE_ERROR = 8
};
#define NT98513_SENSOR_CLI_WRITE_MAX 128u
/* Standalone portable presentation; external I/O is entirely injected.
 * Syntax: nt98513-sensor active. argc 0/1 or exact --help emits
 * "usage: nt98513-sensor active\n" to stdout, exit0, no device callbacks.
 * Any other syntax emits that usage to stderr, exit2, no device callbacks.
 * Missing io/write -> exit8 before any output/query. argv strings and io must
 * be valid immutable caller objects when inspected; argv[0] is ignored.
 *
 * Exact active invokes actual nt98513_sensor_active_query once. Its structural
 * ARGUMENT outcome emits null unavailable diagnostic fields without reading
 * its untouched diagnostic object. Other outcomes preserve owned diagnostics.
 * JSON is one compact line with fixed key order documented in the literal
 * fixture contract; selection is null on all errors, a three-field object
 * {logical_index,value,name} on OK/NOT_SELECTED. Names are fixed library strings.
 * No field denotes physical readiness. Numeric status values remain distinct
 * from the nonnegative shell exits above.
 *
 * All query/close work finishes before output. Chunks are at most128 bytes;
 * writes must return 1..requested bytes. Zero/negative/overlarge -> exit8,
 * no retry or further output. A partial JSON record may already have escaped.
 * Output failure does not undo or repeat an already attempted transaction.
 * The command, lower client/codec/parser and generic CLI are not altered.
 *
 * Stock usys clears shared staging; a name may reflect a failed earlier probe.
 * Prezeroed response tails and no produced length limit publication/freshness
 * claims. Stock ioctl may block indefinitely; watchdog, BSP, readiness,
 * acquisition/retention, capture and recovery remain outside this operation.
 */
int nt98513_sensor_cli(int argc,const char *const *argv,
    const struct nt98513_client_io *io);
#endif

