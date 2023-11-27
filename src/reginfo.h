#ifndef REGINFO_H
#define REGINFO_H

#include <stdint.h>
#include <stdlib.h>

typedef const struct {
    uint32_t address;
    const char *funcs[];
} muxctrl_reg_t;

#define MUXCTRL(name, addr, ...)                                               \
    static muxctrl_reg_t name = {addr, {__VA_ARGS__, 0}};

int reginfo_cmd(int argc, char **argv);
int gpio_cmd(int argc, char **argv);
char *gpio_possible_ircut(char *outbuf, size_t outlen);

#endif /* REGINFO_H */
