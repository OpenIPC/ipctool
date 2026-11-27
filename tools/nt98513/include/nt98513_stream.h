/* SPDX-License-Identifier: MIT */
#ifndef NT98513_STREAM_H
#define NT98513_STREAM_H

#include <stddef.h>
#include <stdint.h>

/* Offline ABI helpers, not an encoder or hardware driver. The byte contracts
 * below are observed in August mipc (SHA-256 4caa14dec2bcdf2316440f065a69d3de
 * 3438ff139841aa003b7c3fcfdced412c). See research/media-findings.md and retained
 * evidence/rea-stream-interface.json:
 * GetVStream ev_443ac7f8bc202fae3dac5a9235592eb9902457380d37e161963314b75f9cca16
 * Open       ev_ac83c2e7c6bbb1d27112524db891e0085ce562303658f05ac908b46e114a2a60
 * FlushCache ev_e54b438eca199ae6349a44c2d7181585b886bdb91ded82048333d8725e8af004
 * Release    ev_3721eaa692bd1580cbb517415dbce7fd8b64de8b9e74d0669a9b8cf96a54b328
 * The independent kernel review in research/stream-driver-review.md resolves
 * GETITEM type, millisecond timeout, cursors and the complete ioctl dispatcher.
 * There is no payload-release ioctl in that dispatcher; RTOS reuse is unknown.
 * None of these helpers accesses a device, shared memory, or mapped payload.
 */

#define NT98513_STREAM_BYTES 56u
#define NT98513_RANGE_BYTES 8u
#define NT98513_CHANNEL_COUNT 16u

/* Observed MIPS constants, deliberately independent of host _IO* macros. */
#define NT98513_IOCTL_GET_STREAM UINT32_C(0x40385301)
#define NT98513_IOCTL_GET_BUFFER_RANGE UINT32_C(0x40085303)
#define NT98513_IOCTL_FLUSH_CACHE UINT32_C(0x80084310)

enum nt98513_status {
    NT98513_OK = 0,
    NT98513_ERR_ARGUMENT = -1,
    NT98513_ERR_SIZE = -2,
    NT98513_ERR_CHANNEL = -3,
    NT98513_ERR_EMPTY = -4,
    NT98513_ERR_RANGE = -5
};

/* Host representation only: never pass this struct directly to ioctl. Unknown
 * words and the complete metadata word are preserved. Historic opaque/raw
 * member names remain stable. Kernel GETITEM uses opaque_00 as type (0 video,
 * nonzero audio), timeout_raw as milliseconds, and zero-extends two 32-bit
 * timestamp inputs into the slots ending at opaque_2c/opaque_34. Timestamp
 * units, metadata enums and RTOS payload lifetime remain unresolved.
 */
struct nt98513_stream_descriptor {
    uint32_t opaque_00;
    uint32_t channel;
    uint32_t timeout_raw;
    uint32_t opaque_0c;
    uint32_t sequence_low;
    uint32_t sequence_high;
    uint32_t payload_address;
    uint32_t payload_length;
    uint32_t metadata;
    uint32_t opaque_24;
    uint32_t timestamp_0;
    uint32_t opaque_2c;
    uint32_t timestamp_1;
    uint32_t opaque_34;
};

struct nt98513_buffer_range {
    uint32_t address;
    uint32_t length;
};

/* All wire buffers use little-endian bytes and require exactly the stated
 * size. Null arguments fail. Errors leave output objects/buffers unchanged.
 * Request selects video (type 0), channel 0..15. Timeout is milliseconds;
 * zero means an indefinite interruptible GETITEM wait, not a nonblocking poll.
 * This byte codec performs no waiting and imposes no additional timeout policy.
 */
enum nt98513_status nt98513_stream_request(uint8_t *wire, size_t size,
                                          uint32_t channel, uint32_t timeout_raw);
enum nt98513_status nt98513_stream_decode(const uint8_t *wire, size_t size,
                                         struct nt98513_stream_descriptor *out);
enum nt98513_status nt98513_buffer_range_decode(const uint8_t *wire, size_t size,
                                               struct nt98513_buffer_range *out);

/* Validates the full nonempty payload span against both the device range and
 * actual mapping length, returning only a byte offset. A mapping may be longer
 * than the device range but never shorter. Range endpoints may equal 2^32;
 * addresses and lengths are each 32-bit and must not wrap past that endpoint.
 * No pointer is constructed or payload read, and cache coherency is not proved.
 */
enum nt98513_status nt98513_stream_payload_offset(
    const struct nt98513_buffer_range *range,
    const struct nt98513_stream_descriptor *stream,
    size_t mapped_size, size_t *offset);

/* Builds the observed cache request {target_virtual_address, length}. */
enum nt98513_status nt98513_cache_request(uint8_t *wire, size_t size,
                                         uint32_t address, uint32_t length);

#endif
