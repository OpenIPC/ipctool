#ifndef REGINFO_H
#define REGINFO_H

#include <stdlib.h>

int reginfo_cmd(int argc, char **argv);
int gpio_cmd(int argc, char **argv);
char *gpio_possible_ircut(char *outbuf, size_t outlen);

#endif /* REGINFO_H */
