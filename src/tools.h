#ifndef TOOLS_H
#define TOOLS_H

#include <regex.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/syscall.h>

#define ADD_PARAM(param, val)                                                  \
    {                                                                          \
        cJSON *strval = cJSON_CreateString(val);                               \
        cJSON_AddItemToObject(j_inner, param, strval);                         \
    }

#define ADD_PARAM_NOTNULL(param, val)                                          \
    {                                                                          \
        if (val != NULL) {                                                     \
            ADD_PARAM(param, val);                                             \
        }                                                                      \
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
#define ARRAY_SIZE(array) \
    (sizeof(array) / sizeof(*array))

enum REG_OPS { OP_READ, OP_WRITE };

int compile_regex(regex_t *r, const char *regex_text);
bool mem_reg(uint32_t addr, uint32_t *data, enum REG_OPS op);
void lsnprintf(char *buf, size_t n, char *fmt, ...);
bool get_regex_line_from_file(const char *filename, const char *re, char *buf,
                              size_t buflen);
int dmesg();
uint32_t read_le32(const char *ptr);
char *file_to_buf(const char *filename, size_t *len);
char *fread_to_buf(const char *filename, size_t *len, uint32_t round_up,
                   size_t *payloadsz);
void restore_printk();
void disable_printk();
uint32_t ceil_up(uint32_t n, uint32_t offset);

// avoid warnings for old compilers
#if __GNUC__ < 7
extern __ssize_t getline(char **__restrict __lineptr, size_t *__restrict __n,
                         FILE *__restrict __stream) __wur;
#endif

#define delete_module(mod, flags) syscall(__NR_delete_module, mod, flags)

#endif /* TOOLS_H */
