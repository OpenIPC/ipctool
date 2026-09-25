#ifndef REGINFO_H
#define REGINFO_H

#include <stdbool.h>
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

/* The level a `gpio set` may write, parsed strictly. strtoul answers 0 for
 * every string that is not a number, and 0 is a level the command really
 * writes -- it switches the pad to an output and drives it low -- so a bare
 * strtoul turned every typo into hardware. The number must end the string
 * and be 0 or 1. */
bool parse_gpio_level(const char *arg, unsigned *level);

/* Which of nwin equal windows, the first at base and each stride wide, one
 * /dev/mem mapping covers. A /dev/mem mapping's file offset is its physical
 * address and its length is the vaddr span it names, so the interval under
 * test is [off, off + len): a daemon mapping one broad window from well
 * below is holding the windows the same as one mapped exactly. */
uint32_t gpio_windows_in_mapping(unsigned long off, unsigned long len,
                                 uint32_t base, uint32_t stride, int nwin);

#endif /* REGINFO_H */
