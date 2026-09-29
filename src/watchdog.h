#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <linux/watchdog.h>

// The HiSilicon SDK watchdog driver (V3 and later) numbers its ioctls its own
// way. On a Hi3516EV300 it also ignores write(), so no standard keepalive
// reaches it.
#define HISINEW_WDIOC_KEEPALIVE _IO(WATCHDOG_IOCTL_BASE, 5)
#define HISINEW_WDIOC_SETOPTIONS _IOWR(WATCHDOG_IOCTL_BASE, 4, int)

int watchdog_cmd(int argc, char *argv[]);

#endif /* WATCHDOG_H */
