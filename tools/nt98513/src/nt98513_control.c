/* SPDX-License-Identifier: MIT */
#include "nt98513_control.h"

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_le32(uint8_t *p, uint32_t value)
{
    p[0]=(uint8_t)value; p[1]=(uint8_t)(value>>8);
    p[2]=(uint8_t)(value>>16); p[3]=(uint8_t)(value>>24);
}

static size_t operation_size(enum nt98513_control_operation op)
{
    switch (op) {
    case NT98513_SEND_COMMAND:
    case NT98513_GET_CONFIG:
    case NT98513_SET_CONFIG: return NT98513_CONTROL_SIZE;
    case NT98513_GET_NOTIFICATION:
    case NT98513_GET_RAW_NOTIFICATION: return NT98513_NOTIFY_SIZE;
    default: return 0;
    }
}

static int valid_span(uint32_t address, uint32_t length)
{
    return address != 0 && length != 0 && length-1 <= UINT32_MAX-address;
}

enum nt98513_control_status nt98513_control_prepare(
    enum nt98513_control_operation op, uint8_t *wire, size_t size,
    const struct nt98513_control_request *request, uint32_t *command)
{
    size_t required = operation_size(op), i;
    uint32_t number;
    if (wire == NULL || request == NULL || command == NULL)
        return NT98513_CONTROL_ARGUMENT;
    if (required == 0) return NT98513_CONTROL_OPERATION;
    if (size != required) return NT98513_CONTROL_SIZE_ERROR;
    if (required == NT98513_CONTROL_SIZE) {
        if (request->command_length == 0 || request->command_length > NT98513_COMMAND_MAX)
            return NT98513_CONTROL_LIMIT;
        if (!valid_span(request->command_address,request->command_length))
            return NT98513_CONTROL_RANGE;
        if (op == NT98513_SEND_COMMAND) {
            if (request->config_address != 0 || request->config_length != 0)
                return NT98513_CONTROL_ARGUMENT;
        } else {
            if (request->config_length == 0 || request->config_length > NT98513_CONFIG_MAX)
                return NT98513_CONTROL_LIMIT;
            if (!valid_span(request->config_address,request->config_length))
                return NT98513_CONTROL_RANGE;
        }
    } else if (request->command_address != 0 || request->command_length != 0 ||
               request->config_address != 0 || request->config_length != 0) {
        return NT98513_CONTROL_ARGUMENT;
    }
    /* Keep capacities positive in the driver's signed comparison paths. */
    if (request->response_capacity > INT32_MAX) return NT98513_CONTROL_LIMIT;
    if (request->response_address != 0 || request->response_capacity != 0 ||
        required == NT98513_NOTIFY_SIZE) {
        if (!valid_span(request->response_address,request->response_capacity))
            return NT98513_CONTROL_RANGE;
    }
    switch (op) {
    case NT98513_SEND_COMMAND: number=UINT32_C(0xc01c552d); break;
    case NT98513_GET_CONFIG: number=UINT32_C(0xc01c552f); break;
    case NT98513_SET_CONFIG: number=UINT32_C(0xc01c5530); break;
    case NT98513_GET_NOTIFICATION: number=UINT32_C(0xc00c552e); break;
    default: number=UINT32_C(0xc00c5531); break;
    }
    for (i=0;i<required;++i) wire[i]=0;
    if (required == NT98513_CONTROL_SIZE) {
        write_le32(wire,request->command_address);
        write_le32(wire+4,request->command_length);
        write_le32(wire+8,request->config_address);
        write_le32(wire+12,request->config_length);
        write_le32(wire+20,request->response_address);
        write_le32(wire+24,request->response_capacity);
    } else {
        write_le32(wire+4,request->response_address);
        write_le32(wire+8,request->response_capacity);
    }
    *command=number;
    return NT98513_CONTROL_OK;
}

enum nt98513_control_status nt98513_control_parse_reply(
    enum nt98513_control_operation op, const uint8_t *wire, size_t size,
    int result, struct nt98513_control_reply *reply)
{
    size_t required=operation_size(op), i;
    uint32_t status;
    if (wire == NULL || reply == NULL) return NT98513_CONTROL_ARGUMENT;
    if (required == 0) return NT98513_CONTROL_OPERATION;
    if (size != required) return NT98513_CONTROL_SIZE_ERROR;
    if (result != 0) return NT98513_CONTROL_IOCTL;
    status=read_le32(wire+(required == NT98513_CONTROL_SIZE ? 16 : 0));
    for (i=0;i<7;++i) reply->words[i]=i<required/4 ? read_le32(wire+4*i) : 0;
    reply->word_count=(uint32_t)(required/4);
    /* Avoid implementation-defined unsigned-to-signed out-of-range casts. */
    reply->remote_status=status<=INT32_MAX ? (int32_t)status :
        -1-(int32_t)(UINT32_MAX-status);
    return NT98513_CONTROL_OK;
}

enum nt98513_control_status nt98513_venc_settings_decode(
    const uint8_t *wire, size_t size, struct nt98513_venc_settings *out)
{
    size_t i;
    if (wire == NULL || out == NULL) return NT98513_CONTROL_ARGUMENT;
    if (size != NT98513_VENC_SIZE) return NT98513_CONTROL_SIZE_ERROR;
    for (i=0;i<NT98513_VENC_WORDS;++i) out->words[i]=read_le32(wire+4*i);
    out->input_source=out->words[0]; out->codec=out->words[1];
    out->profile=out->words[2]; out->width=out->words[3];
    out->height=out->words[4]; out->rate_mode=out->words[5];
    out->frame_delay_divisor_known=1;
    switch (out->rate_mode) {
    case 1: out->frame_delay_divisor=out->words[0x19]; break;
    case 2: out->frame_delay_divisor=out->words[9]; break;
    case 3: out->frame_delay_divisor=out->words[0x26]; break;
    case 4: out->frame_delay_divisor=out->words[0x2e]; break;
    default:
        out->frame_delay_divisor=0; out->frame_delay_divisor_known=0; break;
    }
    return NT98513_CONTROL_OK;
}
