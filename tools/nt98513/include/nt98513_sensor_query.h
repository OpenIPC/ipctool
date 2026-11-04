/* SPDX-License-Identifier: MIT */
#ifndef NT98513_SENSOR_QUERY_H
#define NT98513_SENSOR_QUERY_H
#include "nt98513_client.h"
#include "nt98513_sensor.h"

enum nt98513_sensor_query_status {
    NT98513_SENSOR_QUERY_OK = 0,
    NT98513_SENSOR_QUERY_NOT_SELECTED = 1,
    NT98513_SENSOR_QUERY_ARGUMENT = -1,
    NT98513_SENSOR_QUERY_CLIENT_ERROR = -2,
    NT98513_SENSOR_QUERY_REPLY_ERROR = -3
};
struct nt98513_sensor_query_diagnostic {
    struct nt98513_client_result client;
    unsigned parse_called;
    enum nt98513_control_status parser_status;
};
/* One synchronous SEND_COMMAND "usys -sensor_active 0", with no config.
 * Uses the actual client and strict parser. No heap, adapter, CLI, retry,
 * polling, sensor probe, selection change, preset or readiness operation.
 *
 * io, selection and diagnostic must be valid disjoint caller-owned objects.
 * Input/callback state and output objects must not overlap, and callbacks
 * must obey nt98513_client_io's synchronous mapping/lifetime contract. No
 * overlap or concurrency detection. context may be NULL when callbacks allow
 * it; write is optional and never called. address/open/ioctl/close required.
 * NULL arguments or missing required callbacks -> ARGUMENT, no I/O, all
 * supplied outputs unchanged. After valid arguments diagnostic records the
 * actual attempted transaction, including failures; parse_called is zero
 * and parser_status is ARGUMENT until strict parsing is called.
 *
 * Selection is published only after client success INCLUDING close, exact
 * zero ioctl/remote status, valid bounded terminated response, and successful
 * parsing of response_size+1 bytes. OK publishes a named software selection;
 * NOT_SELECTED publishes OFF. Every error leaves selection unchanged.
 * Diagnostic owns the raw client result; no borrowed buffer views returned.
 * Parser status is meaningful only when parse_called is nonzero.
 *
 * This query clears shared RTOS usys staging and is not globally read-only.
 * A returned name may reflect a failed earlier probe, not fitted hardware.
 * Client prezeroing plus no wire produced-text length means a found NUL does
 * not prove the remote wrote a complete reply. ioctl may block indefinitely;
 * watchdog ownership remains an operational dependency. No physical sensor,
 * mode/preset compatibility, buffer lease, capture or recovery is established.
 */
enum nt98513_sensor_query_status nt98513_sensor_active_query(
    const struct nt98513_client_io *io,
    struct nt98513_sensor_active *selection,
    struct nt98513_sensor_query_diagnostic *diagnostic);
#endif
