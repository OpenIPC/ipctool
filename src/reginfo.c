#include "reginfo.h"
#include "tools.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef const struct {
    uint32_t address;
    const char *funcs[];
} muxctrl_reg_t;

#define MUXCTRL(name, addr, ...)                                               \
    static muxctrl_reg_t name = {addr, {__VA_ARGS__, 0}};

MUXCTRL(cv100_0, 0x200f0000, "GPIO1_0", "SHUTTER_TRIG")
MUXCTRL(cv100_1, 0x200f0004, "GPIO1_1", "SDIO_CCLK_OUT")

static const muxctrl_reg_t *regs[] = {
    &cv100_0,
    &cv100_1,
    0,
};

static const char *get_function(const char *const *func, unsigned val) {
    for (int i = 0; func[i]; i++) {
        if (i == val)
            return func[i] ? func[i] : "reversed";
    }

    return "reversed";
}

int reginfo_cmd(int argc, char **argv) {
    for (int reg_num = 0; regs[reg_num]; reg_num++) {
        uint32_t val;
        if (!mem_reg(regs[reg_num]->address, &val, OP_READ)) {
            printf("read reg %#x error\n", regs[reg_num]->address);
            continue;
        }
        printf("muxctrl_reg%d %#x %s\n", reg_num, regs[reg_num]->address,
               get_function(regs[reg_num]->funcs, val));
    }

    return EXIT_SUCCESS;
}
