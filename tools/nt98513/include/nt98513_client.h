/* SPDX-License-Identifier: MIT */
#ifndef NT98513_CLIENT_H
#define NT98513_CLIENT_H
#include "nt98513_control.h"

/* One synchronous command/config transaction. No automatic command retries,
 * readiness checks or initialization. Stock ioctl may wait indefinitely. */
#define NT98513_CLIENT_DEVICE "/dev/nvt_uctrl"
#define NT98513_CLIENT_OPEN_FLAGS 0x12u
#define NT98513_CLIENT_RESPONSE_SIZE 256u

/* All callbacks are synchronous, returning negative errno on syscall failure.
 * address supplies a non-owning 32-bit target address for the complete buffer;
 * it must not allocate resources and all mappings last through the ioctl.
 * ioctl success is EXACTLY zero; a positive copy residual is an error.
 * close is called once, never retried. write may return a short positive count.
 * No native host device adapter is provided. */
struct nt98513_client_io {
    void *context;
    int32_t (*address)(void *, const void *, size_t, uint32_t *);
    int32_t (*open)(void *, const char *, uint32_t);
    int32_t (*ioctl)(void *, int32_t, uint32_t, uint8_t *, size_t);
    int32_t (*close)(void *, int32_t);
    int32_t (*write)(void *, int32_t, const uint8_t *, size_t);
};
struct nt98513_client_request {
    enum nt98513_control_operation operation;
    const char *command; /* NUL terminated, 1..255 bytes before NUL. */
    uint8_t *config;     /* Caller owned; GET may modify it even on failure. */
    size_t config_size;
};
enum nt98513_client_status {
    NT98513_CLIENT_OK = 0, NT98513_CLIENT_USAGE = 2,
    NT98513_CLIENT_ADDRESS = 3, NT98513_CLIENT_OPEN = 4,
    NT98513_CLIENT_IOCTL = 5, NT98513_CLIENT_REMOTE = 6,
    NT98513_CLIENT_CLOSE = 7, NT98513_CLIENT_WRITE = 8,
    NT98513_CLIENT_RESPONSE = 9
};
struct nt98513_client_result {
    enum nt98513_client_status status;
    enum nt98513_control_status codec_status;
    int32_t address_return, open_return, ioctl_return, close_return;
    unsigned address_called, open_called, opened, ioctl_called, closed;
    unsigned remote_valid, config_valid, response_terminated;
    struct nt98513_control_reply reply;
    uint8_t response[NT98513_CLIENT_RESPONSE_SIZE];
    size_t response_size;
};
/* Input, config and result storage must not overlap. Remote status must equal
 * zero for success; positive and negative statuses are preserved as failures.
 * config_valid describes GET bytes only and requires transport+remote success
 * and a terminated response. It does not establish complete command-specific
 * fill or meaning: the driver returns the requested byte count with no actual
 * produced-length field. GET storage is zeroed before opening the device.
 * A later close/output failure does not undo an already executed command. */
enum nt98513_client_status nt98513_client_run(
    const struct nt98513_client_request *, const struct nt98513_client_io *,
    struct nt98513_client_result *);

/* One JSON line for an attempted operation; hex preserves arbitrary bytes.
 * --help/no args do not open a device. Syntax:
 * command TEXT | get TEXT BYTE_COUNT | set TEXT HEX_BYTES
 * GET outputs config bytes only when config_valid. Notifications unsupported.
 * Work storage is caller owned and at least NT98513_CONFIG_MAX bytes. */
int nt98513_client_cli(int argc, const char *const *argv,
    const struct nt98513_client_io *, uint8_t *work, size_t work_size);
#endif
