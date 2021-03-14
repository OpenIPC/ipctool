#include <stdio.h>
#include <string.h>

#include "chipid.h"
#include "firmware.h"
#include "uboot.h"

bool detect_firmare() {
    const char *uver = uboot_getenv("ver");
    if (uver) {
        const char *stver = strchr(uver, ' ');
        if (stver && *(stver + 1)) {
            snprintf(firmware, sizeof(firmware), "  u-boot: %s\n", stver + 1);
        }
    }

    return true;
}
