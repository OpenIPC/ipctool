/* SPDX-License-Identifier: MIT */
#include "nt98513_sensor_cli.h"
struct writer { const struct nt98513_client_io *io; int32_t fd; unsigned failed; };
static void bytes(struct writer *w,const uint8_t *p,size_t n)
{
    while(n && !w->failed) {
        size_t chunk=n>NT98513_SENSOR_CLI_WRITE_MAX?NT98513_SENSOR_CLI_WRITE_MAX:n;
        int32_t done=w->io->write(w->io->context,w->fd,p,chunk);
        if(done<=0 || (uint32_t)done>chunk) { w->failed=1; return; }
        p+=(size_t)done; n-=(size_t)done;
    }
}
static void text(struct writer *w,const char *s)
{
    size_t n=0;while(s[n])++n;bytes(w,(const uint8_t *)s,n);
}
static void number(struct writer *w,uint32_t v)
{
    uint8_t buf[10];size_t n=sizeof(buf);
    do { buf[--n]=(uint8_t)('0'+v%10u);v/=10u; } while(v);
    bytes(w,buf+n,sizeof(buf)-n);
}
static void signed_number(struct writer *w,int32_t v)
{
    uint32_t magnitude=(uint32_t)v;
    if(v<0) { text(w,"-");magnitude=0u-magnitude; }
    number(w,magnitude);
}
static void boolean(struct writer *w,unsigned v) { text(w,v?"true":"false"); }
static void nullable_signed(struct writer *w,unsigned valid,int32_t v)
{ if(valid)signed_number(w,v);else text(w,"null"); }
static void nullable_bool(struct writer *w,unsigned valid,unsigned v)
{ if(valid)boolean(w,v);else text(w,"null"); }
static int equal(const char *a,const char *b)
{
    if(a==NULL)return 0;
    while(*a && *a==*b){++a;++b;}return *a==*b;
}
static const char *sensor_name(enum nt98513_sensor_selection s)
{
    switch(s) {
    case NT98513_SENSOR_OFF:return "CMOS_OFF";
    case NT98513_SENSOR_OS05A10M:return "CMOS_OS05A10M";
    case NT98513_SENSOR_GC2053M:return "CMOS_GC2053M";
    case NT98513_SENSOR_JXK02M:return "CMOS_JXK02M";
    case NT98513_SENSOR_JXK03M:return "CMOS_JXK03M";
    }
    return ""; /* The actual strict query publishes only the five enums. */
}
static void hex(struct writer *w,const uint8_t *p,size_t n)
{
    static const char digits[]="0123456789abcdef";
    uint8_t buf[128];size_t i;
    while(n && !w->failed) {
        size_t count=n>sizeof(buf)/2?sizeof(buf)/2:n;
        for(i=0;i<count;++i){buf[i*2]=(uint8_t)digits[p[i]>>4];buf[i*2+1]=(uint8_t)digits[p[i]&15];}
        bytes(w,buf,count*2);p+=count;n-=count;
    }
}
static void record(struct writer *w,enum nt98513_sensor_query_status status,
    const struct nt98513_sensor_active *s,const struct nt98513_sensor_query_diagnostic *d)
{
    /* ARGUMENT leaves d untouched. Every expression below that reads it is
     * guarded by valid; a conditional operator evaluates just its chosen arm. */
    unsigned valid=status!=NT98513_SENSOR_QUERY_ARGUMENT;
    unsigned remote=valid?d->client.remote_valid:0;
    unsigned parsed=valid?d->parse_called:0;
    unsigned i;
    text(w,"{\"query_status\":");signed_number(w,(int32_t)status);
    text(w,",\"diagnostic_valid\":");boolean(w,valid);
    text(w,",\"selection\":");
    if(status==NT98513_SENSOR_QUERY_OK || status==NT98513_SENSOR_QUERY_NOT_SELECTED) {
        text(w,"{\"logical_index\":");number(w,s->logical_index);
        text(w,",\"value\":");number(w,(uint32_t)s->selection);
        text(w,",\"name\":\"");text(w,sensor_name(s->selection));text(w,"\"}");
    } else text(w,"null");
    text(w,",\"client_status\":");nullable_signed(w,valid,valid?(int32_t)d->client.status:0);
    text(w,",\"codec_status\":");nullable_signed(w,valid,valid?(int32_t)d->client.codec_status:0);
    text(w,",\"address_return\":");nullable_signed(w,valid,valid?d->client.address_return:0);
    text(w,",\"address_called\":");nullable_bool(w,valid,valid?d->client.address_called:0);
    text(w,",\"open_return\":");nullable_signed(w,valid,valid?d->client.open_return:0);
    text(w,",\"open_called\":");nullable_bool(w,valid,valid?d->client.open_called:0);
    text(w,",\"opened\":");nullable_bool(w,valid,valid?d->client.opened:0);
    text(w,",\"ioctl_return\":");nullable_signed(w,valid,valid?d->client.ioctl_return:0);
    text(w,",\"ioctl_called\":");nullable_bool(w,valid,valid?d->client.ioctl_called:0);
    text(w,",\"close_return\":");nullable_signed(w,valid,valid?d->client.close_return:0);
    text(w,",\"closed\":");nullable_bool(w,valid,valid?d->client.closed:0);
    text(w,",\"remote_valid\":");nullable_bool(w,valid,remote);
    text(w,",\"remote_status\":");nullable_signed(w,remote,remote?d->client.reply.remote_status:0);
    text(w,",\"config_valid\":");nullable_bool(w,valid,valid?d->client.config_valid:0);
    text(w,",\"parse_called\":");boolean(w,parsed);
    text(w,",\"parser_status\":");nullable_signed(w,parsed,parsed?(int32_t)d->parser_status:0);
    text(w,",\"reply_words\":");
    if(remote) {text(w,"[");for(i=0;i<7;++i){if(i)text(w,",");number(w,d->client.reply.words[i]);}text(w,"]");}
    else text(w,"null");
    text(w,",\"response_size\":");
    /* The actual client can publish only an owned count in 0..256. */
    if(valid)number(w,(uint32_t)d->client.response_size);else text(w,"null");
    text(w,",\"response_hex\":");
    if(valid){size_t n=d->client.response_size;text(w,"\"");if(n>sizeof(d->client.response))n=sizeof(d->client.response);hex(w,d->client.response,n);text(w,"\"");}
    else text(w,"null");
    text(w,",\"response_terminated\":");nullable_bool(w,valid,valid?d->client.response_terminated:0);
    text(w,"}\n");
}
int nt98513_sensor_cli(int argc,const char *const *argv,
    const struct nt98513_client_io *io)
{
    static const char usage[]="usage: nt98513-sensor active\n";
    struct writer w;struct nt98513_sensor_active selection;
    struct nt98513_sensor_query_diagnostic diagnostic;
    enum nt98513_sensor_query_status status;int result;
    if(io==NULL || io->write==NULL)return NT98513_SENSOR_CLI_WRITE_ERROR;
    w.io=io;w.fd=1;w.failed=0;
    if(argc==0 || argc==1 || (argc==2 && argv && equal(argv[1],"--help"))) {
        text(&w,usage);return w.failed?NT98513_SENSOR_CLI_WRITE_ERROR:NT98513_SENSOR_CLI_OK;
    }
    if(argc!=2 || argv==NULL || !equal(argv[1],"active")) {
        w.fd=2;text(&w,usage);return w.failed?NT98513_SENSOR_CLI_WRITE_ERROR:NT98513_SENSOR_CLI_USAGE;
    }
    status=nt98513_sensor_active_query(io,&selection,&diagnostic);
    record(&w,status,&selection,&diagnostic);
    switch(status) {
    case NT98513_SENSOR_QUERY_OK:result=NT98513_SENSOR_CLI_OK;break;
    case NT98513_SENSOR_QUERY_NOT_SELECTED:result=NT98513_SENSOR_CLI_NOT_SELECTED;break;
    case NT98513_SENSOR_QUERY_ARGUMENT:result=NT98513_SENSOR_CLI_USAGE;break;
    case NT98513_SENSOR_QUERY_CLIENT_ERROR:result=NT98513_SENSOR_CLI_CLIENT_ERROR;break;
    default:result=NT98513_SENSOR_CLI_REPLY_ERROR;break;
    }
    return w.failed?NT98513_SENSOR_CLI_WRITE_ERROR:result;
}
