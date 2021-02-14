#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/klog.h>
#include <sys/mman.h>
#include <unistd.h>

#include "tools.h"

#define MAX_ERROR_MSG 0x1000
int compile_regex(regex_t *r, const char *regex_text) {
    int status = regcomp(r, regex_text, REG_EXTENDED | REG_NEWLINE | REG_ICASE);
    if (status != 0) {
        char error_message[MAX_ERROR_MSG];
        regerror(status, r, error_message, MAX_ERROR_MSG);
        printf("Regex error compiling '%s': %s\n", regex_text, error_message);
        fflush(stdout);
        return -1;
    }
    return 1;
}

// reads io register value
// call with addr == 0 to cleanup cached resources
bool mem_reg(uint32_t addr, uint32_t *data, enum REG_OPS op) {
    static int mem_fd;
    static char *loaded_area;
    static uint32_t loaded_offset;
    static uint32_t loaded_size;

    uint32_t offset = addr & 0xffff0000;
    uint32_t size = 0xffff;
    if (!addr || loaded_area && offset != loaded_offset) {
        int res = munmap(loaded_area, loaded_size);
        if (res) {
            fprintf(stderr, "read_mem_reg error: %s (%d)\n", strerror(errno),
                    errno);
        }
    }

    if (!addr) {
        close(mem_fd);
        return true;
    }

    if (!mem_fd) {
        mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
        if (mem_fd < 0) {
            fprintf(stderr, "can't open /dev/mem\n");
            return false;
        }
    }

    volatile char *mapped_area;
    if (offset != loaded_offset) {
        mapped_area =
            mmap(NULL, // Any adddress in our space will do
                 size, // Map length
                 PROT_READ |
                     PROT_WRITE, // Enable reading & writting to mapped memory
                 MAP_SHARED,     // Shared with other processes
                 mem_fd,         // File to map
                 offset          // Offset to base address
            );
        if (mapped_area == MAP_FAILED) {
            fprintf(stderr, "read_mem_reg mmap error: %s (%d)\n",
                    strerror(errno), errno);
            return false;
        }
        loaded_area = (char *)mapped_area;
        loaded_size = size;
        loaded_offset = offset;
    } else
        mapped_area = loaded_area;

    if (op == OP_READ)
        *data = *(volatile uint32_t *)(mapped_area + (addr - offset));
    else if (op == OP_WRITE)
        *(volatile uint32_t *)(mapped_area + (addr - offset)) = *data;

    return true;
}

void lprintf(char *fmt, ...) {
    char buf[BUFSIZ];

    va_list argptr;
    va_start(argptr, fmt);
    vsnprintf(buf, sizeof buf, fmt, argptr);
    va_end(argptr);

    char *ptr = buf;
    while (*ptr) {
        *ptr = tolower(*ptr);
        ptr++;
    }
    printf("%s", buf);
}

#define SYSLOG_ACTION_READ_ALL 3
ssize_t get_dmesg_buf(char **buf) {
    int len = klogctl(10, NULL, 0); /* read ring buffer size */
    printf("len = %d\n", len);

    *buf = malloc(len);
    if (!*buf)
        return 0;
    len = klogctl(SYSLOG_ACTION_READ_ALL, *buf, len);
    return len;
}

void dmesg() {
    char *dmesg;
    ssize_t len = get_dmesg_buf(&dmesg);

    if (dmesg) {
        puts(dmesg);
        free(dmesg);
    }
}

// avoid warnings for old compilers
#if __GNUC__ < 7
extern __ssize_t getline(char **__restrict __lineptr, size_t *__restrict __n,
                         FILE *__restrict __stream) __wur;
#endif

bool get_regex_line_from_file(const char *filename, const char *re, char *buf,
                              size_t buflen) {
    long res = false;

    FILE *f = fopen(filename, "r");
    if (!f)
        return false;

    regex_t regex;
    regmatch_t matches[2];
    if (!compile_regex(&regex, re))
        goto exit;

    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    while ((read = getline(&line, &len, f)) != -1) {
        if (regexec(&regex, line, sizeof(matches) / sizeof(matches[0]),
                    (regmatch_t *)&matches, 0) == 0) {
            regoff_t start = matches[1].rm_so;
            regoff_t end = matches[1].rm_eo;

            line[end] = 0;
            strncpy(buf, line + start, buflen);
            res = true;
            break;
        }
    }
    if (line)
        free(line);

exit:
    regfree(&regex);
    fclose(f);
    return res;
}
