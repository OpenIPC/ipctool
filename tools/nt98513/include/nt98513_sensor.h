/* SPDX-License-Identifier: MIT */
#ifndef NT98513_SENSOR_H
#define NT98513_SENSOR_H

#include "nt98513_control.h"

/* Library semantic values, NOT stock registry IDs or persisted fw_resv_0. */
enum nt98513_sensor_selection {
    NT98513_SENSOR_OFF = 0,
    NT98513_SENSOR_OS05A10M = 1,
    NT98513_SENSOR_GC2053M = 2,
    NT98513_SENSOR_JXK02M = 3,
    NT98513_SENSOR_JXK03M = 4
};

struct nt98513_sensor_active {
    uint32_t logical_index;
    enum nt98513_sensor_selection selection;
};

/* Pure strict validation of caller-owned reply content, including exactly
 * one final NUL. The only accepted complete strings are:
 * "0 CMOS_OFF success", "0 CMOS_OS05A10M success",
 * "0 CMOS_GC2053M success", "0 CMOS_JXK02M success",
 * "0 CMOS_JXK03M success". logical_index is always 0.
 *
 * reply_size is actual content length INCLUDING NUL, never buffer capacity.
 * The caller must first locate that NUL within its valid owned bounded span.
 * A nonnull reply describes a valid immutable byte span of reply_size bytes;
 * out is a valid writable object disjoint from that span. Unaligned input is
 * allowed; no overlap/concurrency detection. Null reply/out -> ARGUMENT;
 * size outside 1..256 -> SIZE_ERROR before any input read; any unmatched
 * byte string -> RANGE. All failures leave out and input bytes unchanged.
 *
 * OFF is successfully recognized NOT_SELECTED, never a usable selection.
 * Another name proves only parsed RTOS software selection, not chip identity,
 * probe success, preset compatibility, applied mode, or readiness. No borrowed
 * views or firmware pointers are returned. No heap, I/O, commands or fallback.
 * Original selection/probe limits: research/rtos-sensor-selection.md.
 */
enum nt98513_control_status nt98513_sensor_active_parse(
    const void *reply, size_t reply_size, struct nt98513_sensor_active *out);

#endif
