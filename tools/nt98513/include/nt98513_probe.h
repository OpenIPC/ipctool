/* SPDX-License-Identifier: MIT */
#ifndef NT98513_PROBE_H
#define NT98513_PROBE_H
#include "nt98513_stream.h"

#define NT98513_PROBE_DEVICE "/dev/nvt_stream_sender"
/* Probe policy limits, not silicon or driver limits. */
#define NT98513_PROBE_MAX_COUNT 1000u
#define NT98513_PROBE_MAX_TIMEOUT_MS 60000u
#define NT98513_PROBE_OPEN_FLAGS 0u

/* Normalized syscall contract: negative errno on error. ioctl must return
 * exactly zero; a positive copy residual is not success. close is called once
 * and never retried. write may return a positive short count. No payload I/O. */
struct nt98513_probe_io {
    void *context;
    int32_t (*open)(void *, const char *, uint32_t);
    int32_t (*ioctl)(void *, int32_t, uint32_t, uint8_t *, size_t);
    int32_t (*write)(void *, int32_t, const uint8_t *, size_t);
    int32_t (*close)(void *, int32_t);
};
struct nt98513_probe_options { uint32_t channel, count, timeout_ms; };
enum nt98513_probe_status {
    NT98513_PROBE_OK = 0, NT98513_PROBE_USAGE = 2,
    NT98513_PROBE_OPEN = 3, NT98513_PROBE_RANGE = 4,
    NT98513_PROBE_STREAM = 5, NT98513_PROBE_DESCRIPTOR = 6,
    NT98513_PROBE_RANGE_CHANGED = 7, NT98513_PROBE_WRITE = 8,
    NT98513_PROBE_CLOSE = 9, NT98513_PROBE_TIMEOUT = 10
};
struct nt98513_probe_result {
    enum nt98513_probe_status status;
    int32_t io_return, close_return;
    uint32_t received, emitted;
};
/* Finite request count and nonzero timeout, NOT a wall-clock guarantee.
 * Output includes all raw descriptor bytes, range checks and sequence
 * observations; these do not establish atomic metadata or payload lifetime.
 * The range is re-read after each descriptor; a detected change fails closed.
 * Equal observations cannot exclude a change-and-change-back race. */
enum nt98513_probe_status nt98513_probe_run(
    const struct nt98513_probe_options *, const struct nt98513_probe_io *,
    struct nt98513_probe_result *);
/* No args/--help writes usage only. Explicit syntax:
 * --descriptors CHANNEL COUNT TIMEOUT_MS (decimal, within policy above). */
int nt98513_probe_cli(int argc, const char *const *argv,
                      const struct nt98513_probe_io *io);
#endif
