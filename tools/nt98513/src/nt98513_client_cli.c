/* SPDX-License-Identifier: MIT */
#include "nt98513_client.h"

/* This CLI has no device adapter and uses no libc. The caller supplies all I/O.
 * Writes are bounded and require positive progress. A write failure never
 * repeats the transaction (which may already have changed remote state). */
#define OUTPUT_CHUNK 128u
struct output {
    const struct nt98513_client_io *io;
    int32_t fd;
    int failed;
};
static int same(const char *a,const char *b)
{
    if(a==NULL || b==NULL) return 0;
    while(*a && *a==*b) { ++a; ++b; }
    return *a==*b;
}
static size_t text_size(const char *s)
{
    size_t n=0;
    while(s[n]) ++n;
    return n;
}
static void bytes(struct output *out,const uint8_t *p,size_t n)
{
    while(n && !out->failed) {
        size_t count=n>OUTPUT_CHUNK?OUTPUT_CHUNK:n;
        int32_t written=out->io->write(out->io->context,out->fd,p,count);
        if(written<=0 || (uint32_t)written>count) {
            out->failed=1;
            return;
        }
        p+=(uint32_t)written;
        n-=(uint32_t)written;
    }
}
static void text(struct output *out,const char *s)
{
    bytes(out,(const uint8_t *)s,text_size(s));
}
static void number(struct output *out,int32_t value)
{
    uint8_t buffer[11];
    size_t begin=sizeof(buffer);
    uint32_t magnitude=(uint32_t)value;
    if(value<0) magnitude=0u-magnitude;
    do {
        buffer[--begin]=(uint8_t)('0'+magnitude%10u);
        magnitude/=10u;
    } while(magnitude);
    if(value<0) buffer[--begin]='-';
    bytes(out,buffer+begin,sizeof(buffer)-begin);
}
static void boolean(struct output *out,unsigned value)
{
    text(out,value?"true":"false");
}
static void hex(struct output *out,const uint8_t *p,size_t size)
{
    static const char alphabet[]="0123456789abcdef";
    uint8_t buffer[OUTPUT_CHUNK];
    while(size && !out->failed) {
        size_t n=size>OUTPUT_CHUNK/2?OUTPUT_CHUNK/2:size;
        size_t i;
        for(i=0;i<n;++i) {
            buffer[i*2]=(uint8_t)alphabet[p[i]>>4];
            buffer[i*2+1]=(uint8_t)alphabet[p[i]&15u];
        }
        bytes(out,buffer,n*2);
        p+=n; size-=n;
    }
}
static int nibble(char c)
{
    if(c>='0' && c<='9') return c-'0';
    if(c>='a' && c<='f') return c-'a'+10;
    if(c>='A' && c<='F') return c-'A'+10;
    return -1;
}
static int decimal(const char *s,size_t *value)
{
    size_t n=0;
    if(s==NULL || *s==0) return 0;
    while(*s) {
        unsigned digit;
        if(*s<'0' || *s>'9') return 0;
        digit=(unsigned)(*s-'0');
        if(n>(NT98513_CONFIG_MAX-digit)/10u) return 0;
        n=n*10u+digit; ++s;
    }
    if(n==0) return 0;
    *value=n;
    return 1;
}
static int hex_size(const char *s,size_t *size)
{
    size_t n=0;
    if(s==NULL) return 0;
    while(s[n]) {
        if(n>=NT98513_CONFIG_MAX*2u || nibble(s[n])<0) return 0;
        ++n;
    }
    if(n==0 || n%2u) return 0;
    *size=n/2u;
    return 1;
}
static int command_valid(const char *s)
{
    size_t n=0;
    if(s==NULL) return 0;
    while(n<=NT98513_COMMAND_MAX && s[n]) ++n;
    return n>0 && n<=NT98513_COMMAND_MAX;
}
static int usage(struct output *out,int status)
{
    out->fd=status==0?1:2;
    text(out,"usage: nt98513-client command TEXT | get TEXT BYTE_COUNT | set TEXT HEX_BYTES\n");
    return out->failed?NT98513_CLIENT_WRITE:status;
}
static void outcome(struct output *out,const struct nt98513_client_result *r,
                    const struct nt98513_client_request *request)
{
    size_t response_size=r->response_size;
    if(response_size>sizeof(r->response)) response_size=sizeof(r->response);
    text(out,"{\"status\":"); number(out,(int32_t)r->status);
    text(out,",\"codec_status\":"); number(out,(int32_t)r->codec_status);
    text(out,",\"address_return\":"); number(out,r->address_return);
    text(out,",\"address_called\":"); boolean(out,r->address_called);
    text(out,",\"open_return\":"); number(out,r->open_return);
    text(out,",\"open_called\":"); boolean(out,r->open_called);
    text(out,",\"opened\":"); boolean(out,r->opened);
    text(out,",\"ioctl_return\":"); number(out,r->ioctl_return);
    text(out,",\"ioctl_called\":"); boolean(out,r->ioctl_called);
    text(out,",\"close_return\":"); number(out,r->close_return);
    text(out,",\"closed\":"); boolean(out,r->closed);
    text(out,",\"remote_valid\":"); boolean(out,r->remote_valid);
    text(out,",\"remote_status\":");
    if(r->remote_valid) number(out,r->reply.remote_status);
    else text(out,"null");
    text(out,",\"response_hex\":\""); hex(out,r->response,response_size);
    text(out,"\",\"response_terminated\":"); boolean(out,r->response_terminated);
    text(out,",\"requested_config_length\":"); number(out,(int32_t)request->config_size);
    text(out,",\"config_valid\":"); boolean(out,r->config_valid);
    if(request->operation==NT98513_GET_CONFIG && r->config_valid) {
        text(out,",\"config_hex\":\""); hex(out,request->config,request->config_size);
        text(out,"\"");
    }
    text(out,"}\n");
}
int nt98513_client_cli(int argc,const char *const *argv,
    const struct nt98513_client_io *io,uint8_t *work,size_t work_size)
{
    struct nt98513_client_request request;
    struct nt98513_client_result result;
    struct output out;
    size_t config_size=0,i;
    int status;
    if(io==NULL || io->write==NULL) return NT98513_CLIENT_WRITE;
    out.io=io; out.fd=1; out.failed=0;
    if(argc==0 || argc==1 ||
       (argc==2 && argv!=NULL && same(argv[1],"--help"))) return usage(&out,0);
    if(argc<0 || argv==NULL || argc<3 || argc>4 ||
       work==NULL || work_size<NT98513_CONFIG_MAX ||
       io->address==NULL || io->open==NULL || io->ioctl==NULL || io->close==NULL ||
       !command_valid(argv[2])) return usage(&out,NT98513_CLIENT_USAGE);
    if(same(argv[1],"command") && argc==3) request.operation=NT98513_SEND_COMMAND;
    else if(same(argv[1],"get") && argc==4 && decimal(argv[3],&config_size))
        request.operation=NT98513_GET_CONFIG;
    else if(same(argv[1],"set") && argc==4 && hex_size(argv[3],&config_size))
        request.operation=NT98513_SET_CONFIG;
    else return usage(&out,NT98513_CLIENT_USAGE);
    /* Finish validating all input before decoding or invoking the core. */
    request.command=argv[2]; request.config_size=config_size;
    request.config=config_size?work:NULL;
    if(request.operation==NT98513_SET_CONFIG)
        for(i=0;i<config_size;++i)
            work[i]=(uint8_t)((unsigned)nibble(argv[3][i*2])*16u+(unsigned)nibble(argv[3][i*2+1]));
    status=(int)nt98513_client_run(&request,io,&result);
    outcome(&out,&result,&request);
    return out.failed?NT98513_CLIENT_WRITE:status;
}
