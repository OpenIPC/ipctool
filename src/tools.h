#ifndef TOOLS_H
#define TOOLS_H

#include <regex.h>
#include <stdbool.h>
#include <stdint.h>

#define ADD_PARAM(param, val)                                                  \
    {                                                                          \
        cJSON *strval = cJSON_CreateString(val);                               \
        cJSON_AddItemToObject(j_inner, param, strval);                         \
    }

#define ADD_PARAM_NUM(param, num)                                              \
    {                                                                          \
        cJSON *numval = cJSON_CreateNumber(num);                               \
        cJSON_AddItemToObject(j_inner, param, numval);                         \
    }

#define ADD_PARAM_FMT(param, fmt, ...)                                         \
    {                                                                          \
        char val[1024];                                                        \
        snprintf(val, sizeof(val), fmt, __VA_ARGS__);                          \
        ADD_PARAM(param, val);                                                 \
    }
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

enum REG_OPS { OP_READ, OP_WRITE };

int compile_regex(regex_t *r, const char *regex_text);
bool mem_reg(uint32_t addr, uint32_t *data, enum REG_OPS op);
void lprintf(char *fmt, ...);
bool get_regex_line_from_file(const char *filename, const char *re, char *buf,
                              size_t buflen);
void dmesg();
uint32_t read_le32(const char *ptr);

// avoid warnings for old compilers
#if __GNUC__ < 7
extern __ssize_t getline(char **__restrict __lineptr, size_t *__restrict __n,
                         FILE *__restrict __stream) __wur;
#endif

#endif /* TOOLS_H */
