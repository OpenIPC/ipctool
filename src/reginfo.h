#ifndef REGINFO_H
#define REGINFO_H

#include <stdint.h>
#include <stdlib.h>

#include "padmux_names.h"

/* One pad-mux register: the address, and the functions its selector field
 * chooses between, indexed by the value in that field.
 *
 * The functions are OFFSETS into padmux_names, not pointers, and 0 ends the
 * list. That is the whole reason padmux_names.h exists: a `const char *` here
 * would be a R_ARM_RELATIVE relocation in a position-independent consumer --
 * eight bytes of .rel.dyn on top of the four-byte slot, against a name that
 * averages eleven bytes and is shared by five rows. Measured on a majestic
 * build for hi3516ev200: the V4 tables cost 25,962 bytes as pointers and
 * 11,466 as offsets, and every byte of the difference was relocation.
 *
 * padmux_name() turns an entry back into the `const char *` every caller and
 * every ipchw_padmux_t field still speaks. */
typedef const struct {
    uint32_t address;
    uint16_t funcs[];
} muxctrl_reg_t;

#define MUXCTRL(name, addr, ...)                                               \
    static muxctrl_reg_t name = {addr, {__VA_ARGS__, 0}};

int reginfo_cmd(int argc, char **argv);
int gpio_cmd(int argc, char **argv);
char *gpio_possible_ircut(char *outbuf, size_t outlen);

#endif /* REGINFO_H */
