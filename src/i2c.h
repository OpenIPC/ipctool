#ifndef I2C_H
#define I2C_H

#include <stdbool.h>

int i2cget(int argc, char **argv);
int i2cset(int argc, char **argv);
int i2cdump(int argc, char **argv, bool script_mode);

#endif /* I2C_H */
