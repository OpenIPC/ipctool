#include <stdio.h>

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
