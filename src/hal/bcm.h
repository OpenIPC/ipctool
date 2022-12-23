#ifndef BCM_H
#define BCM_H

#include <stdbool.h>

bool bcm_detect_cpu(char *chip_name);
void bcm_setup_hal();

#endif /* BCM_H */
