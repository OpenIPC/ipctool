/* SPDX-License-Identifier: MIT */
#include "nt98513_sensor_cli.h"
_Static_assert(sizeof(void *)==4 && sizeof(size_t)==4,"MIPS O32 required");
#if !defined(__mips__) || !defined(__MIPSEL__)
#error This entry is only for Linux MIPS little-endian O32
#endif
extern const struct nt98513_client_io nt98513_client_mips_io;
/* Link the unchanged generic CLI too: the accepted adapter retains its own
 * generic main reference. No reliance on linker section garbage collection. */
int nt98513_sensor_cli_main(int argc,const char *const *argv)
{
    return nt98513_sensor_cli(argc,argv,&nt98513_client_mips_io);
}

