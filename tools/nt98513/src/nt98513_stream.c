/* SPDX-License-Identifier: MIT */
#include "nt98513_stream.h"

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

/* Compare the last included byte, avoiding overflowing a 32-bit endpoint. */
static enum nt98513_status check_span(uint32_t address, uint32_t length)
{
    if (length == 0)
        return NT98513_ERR_EMPTY;
    if (length - 1 > UINT32_MAX - address)
        return NT98513_ERR_RANGE;
    return NT98513_OK;
}

enum nt98513_status nt98513_stream_request(uint8_t *wire, size_t size,
                                          uint32_t channel, uint32_t timeout_raw)
{
    size_t i;
    if (wire == NULL)
        return NT98513_ERR_ARGUMENT;
    if (size != NT98513_STREAM_BYTES)
        return NT98513_ERR_SIZE;
    if (channel >= NT98513_CHANNEL_COUNT)
        return NT98513_ERR_CHANNEL;
    for (i = 0; i < NT98513_STREAM_BYTES; ++i)
        wire[i] = 0;
    write_le32(wire + 4, channel);
    write_le32(wire + 8, timeout_raw);
    return NT98513_OK;
}

enum nt98513_status nt98513_stream_decode(const uint8_t *wire, size_t size,
                                         struct nt98513_stream_descriptor *out)
{
    uint32_t words[14];
    size_t i;
    if (wire == NULL || out == NULL)
        return NT98513_ERR_ARGUMENT;
    if (size != NT98513_STREAM_BYTES)
        return NT98513_ERR_SIZE;
    for (i = 0; i < 14; ++i)
        words[i] = read_le32(wire + i * 4);
    out->opaque_00 = words[0];
    out->channel = words[1];
    out->timeout_raw = words[2];
    out->opaque_0c = words[3];
    out->sequence_low = words[4];
    out->sequence_high = words[5];
    out->payload_address = words[6];
    out->payload_length = words[7];
    out->metadata = words[8];
    out->opaque_24 = words[9];
    out->timestamp_0 = words[10];
    out->opaque_2c = words[11];
    out->timestamp_1 = words[12];
    out->opaque_34 = words[13];
    return NT98513_OK;
}

enum nt98513_status nt98513_buffer_range_decode(const uint8_t *wire, size_t size,
                                               struct nt98513_buffer_range *out)
{
    uint32_t address, length;
    enum nt98513_status status;
    if (wire == NULL || out == NULL)
        return NT98513_ERR_ARGUMENT;
    if (size != NT98513_RANGE_BYTES)
        return NT98513_ERR_SIZE;
    address = read_le32(wire);
    length = read_le32(wire + 4);
    status = check_span(address, length);
    if (status != NT98513_OK)
        return status;
    out->address = address;
    out->length = length;
    return NT98513_OK;
}

enum nt98513_status nt98513_stream_payload_offset(
    const struct nt98513_buffer_range *range,
    const struct nt98513_stream_descriptor *stream,
    size_t mapped_size, size_t *offset)
{
    uint32_t delta;
    enum nt98513_status status;
    if (range == NULL || stream == NULL || offset == NULL)
        return NT98513_ERR_ARGUMENT;
    status = check_span(range->address, range->length);
    if (status != NT98513_OK)
        return status;
    status = check_span(stream->payload_address, stream->payload_length);
    if (status != NT98513_OK)
        return status;
    if (range->length > mapped_size || stream->payload_address < range->address)
        return NT98513_ERR_RANGE;
    delta = stream->payload_address - range->address;
    if (delta >= range->length || stream->payload_length > range->length - delta)
        return NT98513_ERR_RANGE;
    *offset = (size_t)delta;
    return NT98513_OK;
}

enum nt98513_status nt98513_cache_request(uint8_t *wire, size_t size,
                                         uint32_t address, uint32_t length)
{
    enum nt98513_status status;
    if (wire == NULL)
        return NT98513_ERR_ARGUMENT;
    if (size != NT98513_RANGE_BYTES)
        return NT98513_ERR_SIZE;
    status = check_span(address, length);
    if (status != NT98513_OK)
        return status;
    write_le32(wire, address);
    write_le32(wire + 4, length);
    return NT98513_OK;
}
