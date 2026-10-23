/* SPDX-License-Identifier: MIT */
#include "nt98513_client.h"
_Static_assert(sizeof(void *)==4 && sizeof(size_t)==4,"MIPS O32 required");
#if !defined(__mips__) || !defined(__MIPSEL__)
#error This adapter is only for Linux MIPS little-endian O32
#endif
extern int32_t nt98513_client_syscall3(uint32_t,uint32_t,uint32_t,uint32_t);
static int32_t buffer_address(void *ctx,const void *p,size_t size,uint32_t *address)
{
    uint32_t start=(uint32_t)(uintptr_t)p;
    (void)ctx;
    /* Arithmetic only: the caller owns and keeps the storage alive. This does
     * not establish access permissions, a driver mapping or a buffer lease. */
    if(p==NULL || address==NULL || size==0 || size-1u>UINT32_MAX-start)
        return -22;
    *address=start;
    return 0;
}
static int32_t device_open(void *ctx,const char *path,uint32_t flags)
{ (void)ctx; return nt98513_client_syscall3(4005,(uint32_t)(uintptr_t)path,flags,0); }
static int32_t device_ioctl(void *ctx,int32_t fd,uint32_t request,uint8_t *bytes,size_t size)
{ (void)ctx; (void)size; return nt98513_client_syscall3(4054,(uint32_t)fd,request,(uint32_t)(uintptr_t)bytes); }
static int32_t device_close(void *ctx,int32_t fd)
{ (void)ctx; return nt98513_client_syscall3(4006,(uint32_t)fd,0,0); }
static int32_t output_write(void *ctx,int32_t fd,const uint8_t *bytes,size_t size)
{ (void)ctx; return nt98513_client_syscall3(4004,(uint32_t)fd,(uint32_t)(uintptr_t)bytes,(uint32_t)size); }
const struct nt98513_client_io nt98513_client_mips_io={
    0,buffer_address,device_open,device_ioctl,device_close,output_write
};
int nt98513_client_main(int argc,const char *const *argv)
{
    static uint8_t workspace[NT98513_CONFIG_MAX];
    return nt98513_client_cli(argc,argv,&nt98513_client_mips_io,workspace,sizeof(workspace));
}
