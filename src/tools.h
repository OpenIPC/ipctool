#ifndef TOOLS_H
#define TOOLS_H

#include <regex.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <sys/types.h>

#define ARRCNT(a) (sizeof(a) / sizeof((a)[0]))

#define ADD_PARAM(param, val)                                                  \
    do {                                                                       \
        cJSON *strval = cJSON_CreateString(val);                               \
        cJSON_AddItemToObject(j_inner, param, strval);                         \
    } while (0)

#define ADD_PARAM_NOTNULL(param, val)                                          \
    do {                                                                       \
        if (val != NULL)                                                       \
            ADD_PARAM(param, val);                                             \
    } while (0)

#define ADD_PARAM_NUM(param, num)                                              \
    do {                                                                       \
        cJSON *numval = cJSON_CreateNumber(num);                               \
        cJSON_AddItemToObject(j_inner, param, numval);                         \
    } while (0)

#define ADD_PARAM_FMT(param, fmt, ...)                                         \
    do {                                                                       \
        char val[1024];                                                        \
        snprintf(val, sizeof(val), fmt, __VA_ARGS__);                          \
        ADD_PARAM(param, val);                                                 \
    } while (0)
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

enum REG_OPS { OP_READ, OP_WRITE };

int regex_compile(regex_t *r, const char *regex_text);
bool mem_reg(uint32_t addr, uint32_t *data, enum REG_OPS op);
void lsnprintf(char *buf, size_t n, char *fmt, ...);
bool dts_items_by_regex(const char *filename, const char *re, char *outbuf,
                        size_t outlen);
bool line_from_file(const char *filename, const char *re, char *outbuf,
                    size_t outlen);
int dmesg();
uint32_t read_le32(const char *ptr);
char *file_to_buf(const char *filename, size_t *len);
char *fread_to_buf(const char *filename, size_t *len, uint32_t round_up,
                   size_t *payloadsz);
void restore_printk();
void disable_printk();
uint32_t ceil_up(uint32_t n, uint32_t offset);
pid_t get_god_pid(char *shortname, size_t shortsz);
bool get_pid_cmdline(pid_t godpid, char *cmdname);

// avoid warnings for old compilers
#if __GNUC__ < 7
extern __ssize_t getline(char **__restrict __lineptr, size_t *__restrict __n,
                         FILE *__restrict __stream) __wur;
#endif

#define delete_module(mod, flags) syscall(__NR_delete_module, mod, flags)

#endif /* TOOLS_H */
