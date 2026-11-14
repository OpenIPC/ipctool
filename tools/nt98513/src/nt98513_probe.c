/* SPDX-License-Identifier: MIT */
#include "nt98513_probe.h"

struct line { uint8_t bytes[512]; size_t length; };
static void text(struct line *l, const char *s)
{
    while (*s && l->length < sizeof(l->bytes)) l->bytes[l->length++]=(uint8_t)*s++;
}
static void hex(struct line *l, uint32_t n)
{
    unsigned i; static const char digits[]="0123456789abcdef";
    for(i=0;i<8;++i) l->bytes[l->length++]=(uint8_t)digits[(n>>(28-i*4))&15];
}
/* Every call formats fixed fields; the largest line is under 256 bytes. */
static int32_t write_line(const struct nt98513_probe_io *io,int32_t fd,struct line *l)
{
    size_t done=0;
    while(done<l->length) {
        int32_t n=io->write(io->context,fd,l->bytes+done,l->length-done);
        if(n==0) return -5; /* no progress: normalized EIO */
        if(n<0 || (uint32_t)n>l->length-done) return n;
        done+=(size_t)n;
    }
    return 0;
}
static int valid_io(const struct nt98513_probe_io *io)
{ return io && io->open && io->ioctl && io->write && io->close; }
static int valid_options(const struct nt98513_probe_options *o)
{
    return o && o->channel<NT98513_CHANNEL_COUNT && o->count>0 &&
        o->count<=NT98513_PROBE_MAX_COUNT && o->timeout_ms>0 &&
        o->timeout_ms<=NT98513_PROBE_MAX_TIMEOUT_MS;
}
static int32_t get_range(const struct nt98513_probe_io *io,int32_t fd,
                          struct nt98513_buffer_range *range, int *valid)
{
    uint8_t bytes[8]; size_t i; int32_t rc;
    for(i=0;i<sizeof(bytes);++i) bytes[i]=0;
    rc=io->ioctl(io->context,fd,NT98513_IOCTL_GET_BUFFER_RANGE,bytes,sizeof(bytes));
    *valid=rc==0 && nt98513_buffer_range_decode(bytes,sizeof(bytes),range)==NT98513_OK;
    return rc;
}
static const char *sequence(uint64_t old,uint64_t current,int have_old)
{
    if(!have_old) return "first";
    if(current==old) return "duplicate";
    if(current<old) return "regression";
    if(current-old==1) return "next";
    return "gap";
}
static void descriptor_line(struct line *l,const uint8_t *bytes,size_t offset,
                             int valid,const char *seq)
{
    unsigned i; static const char digits[]="0123456789abcdef";
    l->length=0;text(l,"descriptor raw=");
    for(i=0;i<NT98513_STREAM_BYTES;++i) {
        l->bytes[l->length++]=(uint8_t)digits[bytes[i]>>4];
        l->bytes[l->length++]=(uint8_t)digits[bytes[i]&15];
    }
    text(l,valid?" span=valid offset=":" span=invalid offset=");hex(l,(uint32_t)offset);
    text(l," sequence=");text(l,seq);text(l,"\n");
}
enum nt98513_probe_status nt98513_probe_run(const struct nt98513_probe_options *o,
 const struct nt98513_probe_io *io,struct nt98513_probe_result *r)
{
    int32_t fd,rc;int valid,have_old=0;uint32_t i;uint64_t previous=0;
    struct nt98513_buffer_range range,after;struct line l;
    if(!r) return NT98513_PROBE_USAGE;
    r->status=NT98513_PROBE_USAGE;r->io_return=0;r->close_return=0;r->received=0;r->emitted=0;
    if(!valid_options(o) || !valid_io(io)) return r->status;
    fd=io->open(io->context,NT98513_PROBE_DEVICE,NT98513_PROBE_OPEN_FLAGS);
    if(fd<0) { r->io_return=fd;r->status=NT98513_PROBE_OPEN;return r->status; }
    rc=get_range(io,fd,&range,&valid);
    if(!valid) { r->io_return=rc;r->status=NT98513_PROBE_RANGE;goto close; }
    l.length=0;text(&l,"range address=");hex(&l,range.address);text(&l," length=");hex(&l,range.length);text(&l,"\n");
    rc=write_line(io,1,&l);
    if(rc!=0) { r->io_return=rc;r->status=NT98513_PROBE_WRITE;goto close; }
    r->status=NT98513_PROBE_OK;
    for(i=0;i<o->count;++i) {
        uint8_t bytes[NT98513_STREAM_BYTES];struct nt98513_stream_descriptor d;
        size_t offset=0;uint64_t current;
        (void)nt98513_stream_request(bytes,sizeof(bytes),o->channel,o->timeout_ms);
        rc=io->ioctl(io->context,fd,NT98513_IOCTL_GET_STREAM,bytes,sizeof(bytes));
        if(rc!=0) { r->io_return=rc;r->status=rc==-62?NT98513_PROBE_TIMEOUT:NT98513_PROBE_STREAM;break; }
        ++r->received;
        (void)nt98513_stream_decode(bytes,sizeof(bytes),&d);
        current=((uint64_t)d.sequence_high<<32)|d.sequence_low;
        valid=d.opaque_00==0 && d.channel==o->channel && d.timeout_raw==o->timeout_ms &&
            nt98513_stream_payload_offset(&range,&d,(size_t)range.length,&offset)==NT98513_OK;
        if(!valid) r->status=NT98513_PROBE_DESCRIPTOR;
        else {
            rc=get_range(io,fd,&after,&valid);
            if(!valid) { r->io_return=rc;r->status=NT98513_PROBE_RANGE; }
            else if(after.address!=range.address || after.length!=range.length) {
                valid=0;r->status=NT98513_PROBE_RANGE_CHANGED;
            }
        }
        descriptor_line(&l,bytes,offset,valid,sequence(previous,current,have_old));
        rc=write_line(io,1,&l);
        if(rc!=0) { r->io_return=rc;r->status=NT98513_PROBE_WRITE;break; }
        if(r->status!=NT98513_PROBE_OK) break;
        ++r->emitted;previous=current;have_old=1;
    }
close:
    r->close_return=io->close(io->context,fd);
    if(r->status==NT98513_PROBE_OK && r->close_return!=0) r->status=NT98513_PROBE_CLOSE;
    return r->status;
}
static int same(const char *a,const char *b)
{ if(!a || !b) return 0;while(*a && *a==*b) { ++a;++b; }return *a==*b; }
static int number(const char *s,uint32_t *out)
{
    uint32_t n=0;if(!s || !*s) return 0;
    while(*s) {
        unsigned digit=(unsigned)((unsigned char)*s++-'0');
        if(digit>9 || n>(UINT32_MAX-digit)/10) return 0;
        n=n*10+digit;
    }
    *out=n;return 1;
}
int nt98513_probe_cli(int argc,const char *const *argv,const struct nt98513_probe_io *io)
{
    struct nt98513_probe_options o;struct nt98513_probe_result r;struct line l;
    int help=argc==1 || (argc==2 && argv && same(argv[1],"--help"));
    if(!valid_io(io)) return NT98513_PROBE_USAGE;
    if(help || argc!=5 || !argv || !same(argv[1],"--descriptors") ||
       !number(argv[2],&o.channel) || !number(argv[3],&o.count) ||
       !number(argv[4],&o.timeout_ms) || !valid_options(&o)) {
        l.length=0;text(&l,"Usage: nt98513-descriptor-probe --descriptors CHANNEL COUNT TIMEOUT_MS\n"
        "CHANNEL 0..15; COUNT 1..1000; TIMEOUT_MS 1..60000.\n"
        "Descriptor metadata only; no payload lease or capture. Timeout is not a wall-clock bound.\n");
        if(write_line(io,help?1:2,&l)!=0) return NT98513_PROBE_WRITE;
        return help?0:NT98513_PROBE_USAGE;
    }
    (void)nt98513_probe_run(&o,io,&r);
    l.length=0;text(&l,"result code=");hex(&l,(uint32_t)r.status);text(&l," io=");hex(&l,(uint32_t)r.io_return);
    text(&l," close=");hex(&l,(uint32_t)r.close_return);text(&l," received=");hex(&l,r.received);
    text(&l," emitted=");hex(&l,r.emitted);text(&l,"\n");
    if(write_line(io,2,&l)!=0) return NT98513_PROBE_WRITE;
    return (int)r.status;
}
