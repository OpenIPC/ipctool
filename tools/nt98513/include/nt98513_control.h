/* SPDX-License-Identifier: MIT */
#ifndef NT98513_CONTROL_H
#define NT98513_CONTROL_H

#include <stddef.h>
#include <stdint.h>

/* Independently written byte codec for the observed August 828 transport.
 * REA ev_3a2c1745a2b552d76523d83e66e0aa8de1470ec81c378173b6b1c1f0da626dfd
 * and evidence/nvt-uctrl.asm (.text+0xbc dispatcher) establish these records.
 * Addresses are target-process 32-bit virtual addresses, never host pointers.
 * This API does not execute ioctls. Command/config ioctls can block indefinitely
 * in the stock kernel's wait_for_completion; there is no timeout record field.
 */
#define NT98513_CONTROL_SIZE 28u
#define NT98513_NOTIFY_SIZE 12u
#define NT98513_COMMAND_MAX 255u
#define NT98513_CONFIG_MAX 40960u
#define NT98513_VENC_SIZE 324u
#define NT98513_VENC_WORDS 81u

enum nt98513_control_status {
    NT98513_CONTROL_OK = 0,
    NT98513_CONTROL_ARGUMENT = -1,
    NT98513_CONTROL_SIZE_ERROR = -2,
    NT98513_CONTROL_OPERATION = -3,
    NT98513_CONTROL_RANGE = -4,
    NT98513_CONTROL_LIMIT = -5,
    NT98513_CONTROL_IOCTL = -6
};

enum nt98513_control_operation {
    NT98513_SEND_COMMAND = 1,
    NT98513_GET_CONFIG,
    NT98513_SET_CONFIG,
    NT98513_GET_NOTIFICATION,
    NT98513_GET_RAW_NOTIFICATION
};

struct nt98513_control_request {
    uint32_t command_address;
    uint32_t command_length; /* 1..255 bytes, excludes NUL; driver appends NUL. */
    uint32_t config_address;
    uint32_t config_length; /* 1..40960 for GET/SET; otherwise both zero. */
    uint32_t response_address;
    uint32_t response_capacity;
};

/* All words preserved. Count is 7 or 3; unused words in notify replies are zero.
 * A successful parse does NOT imply successful remote command execution.
 * Inspect remote_status separately. Notify word 2 is capacity for text, or the
 * updated raw length for raw notifications. Text response length is not given.
 */
struct nt98513_control_reply {
    uint32_t words[7];
    uint32_t word_count;
    int32_t remote_status;
};

/* Outputs unchanged on failure. Input/output storage must not overlap.
 * Numeric span checks establish no process mapping/permissions. Optional reply
 * address/capacity must both be zero or both nonzero; notifications require both.
 * Capacities are limited to INT32_MAX to avoid driver's signed-length paths.
 */
enum nt98513_control_status nt98513_control_prepare(
    enum nt98513_control_operation operation, uint8_t *wire, size_t size,
    const struct nt98513_control_request *request, uint32_t *ioctl_request);

/* Reject any nonzero ioctl return, including positive copy residuals; errno is
 * managed by the caller. Returned remote errors remain successful decodes.
 */
enum nt98513_control_status nt98513_control_parse_reply(
    enum nt98513_control_operation operation, const uint8_t *wire, size_t size,
    int ioctl_return, struct nt98513_control_reply *reply);

/* Read-only view; preserve unknown words and never synthesize a setting blob.
 * Geometry may represent source size when independent ISP crop is active.
 * frame_delay_divisor is the word used by exec_venc_run's 1000000/divisor delay,
 * not a measured frame rate. Its meaning is known only for rate modes 1..4.
 * REA ev_4092b6c3efecbd6bf052e9720fa906bd9827ff1d4eb36f2be9cbdf2e494ebd34;
 * see research/control-transport.md for function-boundary qualifications.
 */
struct nt98513_venc_settings {
    uint32_t words[NT98513_VENC_WORDS];
    uint32_t input_source, codec, profile, width, height, rate_mode;
    uint32_t frame_delay_divisor;
    int frame_delay_divisor_known;
};

enum nt98513_control_status nt98513_venc_settings_decode(
    const uint8_t *wire, size_t size, struct nt98513_venc_settings *settings);

#endif
