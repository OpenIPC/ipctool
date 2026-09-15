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

/* OP_*_16 reach a 16-bit port. SigmaStar's RIU banks are half-word registers
 * in four-byte slots whose upper half is not mapped at all, so a 32-bit store
 * there writes two bytes that do not exist. */
enum REG_OPS { OP_READ, OP_WRITE, OP_READ_16, OP_WRITE_16 };

int regex_compile(regex_t *r, const char *regex_text);
/* Read or write one register through /dev/mem, 32 bits wide or 16.
 *
 * NOT thread-safe and not reentrant: the mapping, its offset, its size and the
 * file descriptor are four function-local statics with no lock around them, and
 * any address outside the cached window unmaps it and maps another. Two threads
 * on different windows will unmap the mapping the other is about to
 * dereference. Every caller must come from one thread.
 *
 * Returns false only when /dev/mem cannot be opened or mmapped -- a bad address
 * on live silicon raises SIGBUS rather than returning. Call with addr == 0 to
 * release the window and close the descriptor. */
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

#define delete_module(mod, flags) syscall(__NR_delete_module, mod, flags)

#endif /* TOOLS_H */
