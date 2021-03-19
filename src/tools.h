#ifndef TOOLS_H
#define TOOLS_H

#include <regex.h>
#include <stdbool.h>
#include <stdint.h>

enum REG_OPS { OP_READ, OP_WRITE };

int compile_regex(regex_t *r, const char *regex_text);
bool mem_reg(uint32_t addr, uint32_t *data, enum REG_OPS op);
void lprintf(char *fmt, ...);
bool get_regex_line_from_file(const char *filename, const char *re, char *buf,
                              size_t buflen);
void dmesg();

// avoid warnings for old compilers
#if __GNUC__ < 7
extern __ssize_t getline(char **__restrict __lineptr, size_t *__restrict __n,
                         FILE *__restrict __stream) __wur;
#endif

#endif /* TOOLS_H */
