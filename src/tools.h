#ifndef TOOLS_H
#define TOOLS_H

#include <regex.h>
#include <stdbool.h>
#include <stdint.h>

int compile_regex(regex_t *r, const char *regex_text);
bool read_mem_reg(uint32_t addr, uint32_t *output);

#endif /* TOOLS_H */
