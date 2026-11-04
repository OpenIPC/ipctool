/* SPDX-License-Identifier: MIT */
#include "nt98513_probe.h"
_Static_assert(sizeof(void *)==4 && sizeof(size_t)==4, "MIPS O32 required");
#if !defined(__mips__) || !defined(__MIPSEL__)
#error This adapter is only for Linux MIPS little-endian O32
#endif
extern int32_t nt98513_syscall3(uint32_t,uint32_t,uint32_t,uint32_t);
static int32_t device_open(void *ctx,const char *path,uint32_t flags)
{ (void)ctx;return nt98513_syscall3(4005,(uint32_t)(uintptr_t)path,flags,0); }
static int32_t device_ioctl(void *ctx,int32_t fd,uint32_t request,uint8_t *bytes,size_t size)
{ (void)ctx;(void)size;return nt98513_syscall3(4054,(uint32_t)fd,request,(uint32_t)(uintptr_t)bytes); }
static int32_t output_write(void *ctx,int32_t fd,const uint8_t *bytes,size_t size)
{ (void)ctx;return nt98513_syscall3(4004,(uint32_t)fd,(uint32_t)(uintptr_t)bytes,(uint32_t)size); }
static int32_t device_close(void *ctx,int32_t fd)
{ (void)ctx;return nt98513_syscall3(4006,(uint32_t)fd,0,0); }
int nt98513_probe_main(int argc,const char *const *argv)
{
    struct nt98513_probe_io io;
    io.context=0;io.open=device_open;io.ioctl=device_ioctl;io.write=output_write;io.close=device_close;
    return nt98513_probe_cli(argc,argv,&io);
}
