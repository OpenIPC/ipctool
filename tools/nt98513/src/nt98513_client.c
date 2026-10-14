/* SPDX-License-Identifier: MIT */
#include "nt98513_client.h"
static void zero(void *p,size_t n)
{
    volatile uint8_t *b=p;
    while(n--) *b++=0;
}

enum nt98513_client_status nt98513_client_run(
    const struct nt98513_client_request *request, const struct nt98513_client_io *io,
    struct nt98513_client_result *result)
{
    struct nt98513_control_request raw;
    uint8_t wire[NT98513_CONTROL_SIZE];
    uint32_t number;
    size_t length=0;
    if(result==NULL) return NT98513_CLIENT_USAGE;
    zero(result,sizeof(*result));
    result->status=NT98513_CLIENT_USAGE;
    if(request==NULL || io==NULL || request->command==NULL ||
       io->address==NULL || io->open==NULL || io->ioctl==NULL || io->close==NULL)
        return result->status;
    if(request->operation!=NT98513_SEND_COMMAND &&
       request->operation!=NT98513_GET_CONFIG && request->operation!=NT98513_SET_CONFIG)
        return result->status;
    while(length<=NT98513_COMMAND_MAX && request->command[length]) ++length;
    if(length==0 || length>NT98513_COMMAND_MAX) return result->status;
    if(request->operation==NT98513_SEND_COMMAND) {
        if(request->config!=NULL || request->config_size!=0) return result->status;
    } else if(request->config==NULL || request->config_size==0 ||
              request->config_size>NT98513_CONFIG_MAX) return result->status;
    zero(&raw,sizeof(raw));
    raw.command_length=(uint32_t)length;
    raw.config_length=(uint32_t)request->config_size;
    raw.response_capacity=NT98513_CLIENT_RESPONSE_SIZE;
    result->status=NT98513_CLIENT_ADDRESS;
    result->address_called=1;
    result->address_return=io->address(io->context,request->command,length,&raw.command_address);
    if(result->address_return!=0) return result->status;
    if(request->config_size) {
        result->address_return=io->address(io->context,request->config,
                                          request->config_size,&raw.config_address);
        if(result->address_return!=0) return result->status;
    }
    result->address_return=io->address(io->context,result->response,
                                     sizeof(result->response),&raw.response_address);
    if(result->address_return!=0) return result->status;
    result->codec_status=nt98513_control_prepare(request->operation,wire,sizeof(wire),&raw,&number);
    if(result->codec_status!=NT98513_CONTROL_OK) return result->status;
    if(request->operation==NT98513_GET_CONFIG) zero(request->config,request->config_size);
    result->open_called=1;
    result->open_return=io->open(io->context,NT98513_CLIENT_DEVICE,NT98513_CLIENT_OPEN_FLAGS);
    if(result->open_return<0) {
        result->status=NT98513_CLIENT_OPEN;
        return result->status;
    }
    result->opened=1;
    result->ioctl_called=1;
    result->ioctl_return=io->ioctl(io->context,result->open_return,number,wire,sizeof(wire));
    result->codec_status=nt98513_control_parse_reply(request->operation,wire,sizeof(wire),
                                                    result->ioctl_return,&result->reply);
    if(result->codec_status!=NT98513_CONTROL_OK) result->status=NT98513_CLIENT_IOCTL;
    else {
        result->remote_valid=1;
        while(result->response_size<sizeof(result->response) &&
              result->response[result->response_size]) ++result->response_size;
        result->response_terminated=result->response_size<sizeof(result->response);
        result->status=result->reply.remote_status==0?NT98513_CLIENT_OK:NT98513_CLIENT_REMOTE;
        if(result->response_size==sizeof(result->response)) {
            if(result->status==NT98513_CLIENT_OK) result->status=NT98513_CLIENT_RESPONSE;
        } else if(result->status==NT98513_CLIENT_OK && request->operation==NT98513_GET_CONFIG)
            result->config_valid=1;
    }
    result->close_return=io->close(io->context,result->open_return);
    result->closed=1;
    if(result->close_return!=0 && result->status==NT98513_CLIENT_OK)
        result->status=NT98513_CLIENT_CLOSE;
    return result->status;
}
