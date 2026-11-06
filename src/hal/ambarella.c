#include "hal/ambarella.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hal/common.h"
#include "tools.h"

/* Defaults of the `addr` module parameter across the S3L SDK's VIN drivers,
 * the 8-bit form ipctool's tables use. AR0521 is the one part left out: it
 * answers on 0x6C, and the onsemi probe runs before the OmniVision one, so
 * listing it would have that probe reading an OV4689. */
static unsigned char sony_addrs[] = {0x34, 0};
static unsigned char onsemi_addrs[] = {0x20, 0};
static unsigned char omni_addrs[] = {0x6c, 0};

static sensor_addr_t ambarella_possible_i2c_addrs[] = {
    {SENSOR_SONY, sony_addrs},
    {SENSOR_ONSEMI, onsemi_addrs},
    {SENSOR_OMNIVISION, omni_addrs},
    {0, NULL}};

/* Every DT_MACHINE_START in the vendor tree reads "Ambarella <soc> (Flattened
 * Device Tree)". These kernels register no UART with /proc/iomem, and S3L
 * and S3LM share one machine string: the exact part (33M, 55M, 66, ...) is
 * known only to the DSP firmware once it runs, so "S3L" is as far as this
 * goes. */
bool ambarella_machine_name(const char *cpuinfo, char *out, size_t outlen) {
    if (!outlen)
        return false;

    if (!line_from_file(cpuinfo, "Hardware.+: Ambarella ([A-Za-z0-9]+)", out,
                        outlen))
        return false;

    out[outlen - 1] = '\0';
    return true;
}

bool ambarella_detect_cpu(char *chip_name) {
    char machine[128];

    if (!ambarella_machine_name("/proc/cpuinfo", machine, sizeof(machine)))
        return false;
    strcpy(chip_name, machine);

    /* chip_generation stays 0 on purpose. It keys the pad-mux, clock and boot
     * ROM tables, and ipctool has none for Ambarella, so 0 sends those paths to
     * their no-table answer and getchipfamily() back to the name. */
    return true;
}

#ifndef STANDALONE_LIBRARY
/* /iavmem is what the kernel itself parses, two big-endian cells. Dahua's
 * 3.10.73 has no /proc/device-tree at all (#266), hence the log below. */
bool ambarella_iavmem_from_dt(const char *path, uint32_t *start,
                              uint32_t *size) {
    uint32_t cells[2];
    FILE *f = fopen(path, "rb");

    if (!f)
        return false;
    bool res = fread(cells, sizeof(cells), 1, f) == 1;
    fclose(f);
    if (!res)
        return false;

    *start = ntohl(cells[0]);
    *size = ntohl(cells[1]);
    return true;
}

/* "Ambarella:   IAVMEM = 0x07800000[          ],0x18800000". Printed once at
 * boot, so a long-lived ring buffer may have dropped it. The newline is part
 * of the match: without it a log cut off inside the size would still parse,
 * as a smaller number. */
bool ambarella_iavmem_from_log(const char *log, uint32_t *start,
                               uint32_t *size) {
    const char *line = strstr(log, "IAVMEM = ");
    char end = 0;

    if (!line)
        return false;
    return sscanf(line, "IAVMEM = 0x%x[%*[^]]],0x%x%c", start, size, &end) ==
               3 &&
           end == '\n';
}

static unsigned long ambarella_totalmem(unsigned long *media_mem) {
    uint32_t start = 0, size = 0;
    bool found = ambarella_iavmem_from_dt("/proc/device-tree/iavmem/reg",
                                          &start, &size);

    if (!found) {
        char *log = NULL;
        ssize_t len = get_dmesg_buf(&log);

        /* klogctl() does not terminate what it returns, and the buffer is
         * exactly the size of a full ring. */
        char *text = log && len > 0 ? realloc(log, len + 1) : NULL;

        if (text) {
            log = text;
            log[len] = '\0';
            found = ambarella_iavmem_from_log(log, &start, &size);
        }
        free(log);
    }

    /* Nothing to go on, or a carve-out that wraps: what the kernel sees. */
    if (!found || !size || start + size < start)
        return kernel_mem();

    *media_mem = size / 1024;
    return (unsigned long)(start / 1024) + *media_mem;
}
#endif

void ambarella_setup_hal() {
    possible_i2c_addrs = ambarella_possible_i2c_addrs;
#ifndef STANDALONE_LIBRARY
    hal_totalmem = ambarella_totalmem;
#endif
}
