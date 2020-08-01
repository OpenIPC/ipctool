#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <fcntl.h>
#include <sys/errno.h>
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
