/* Hardware-free cover for the Ambarella machine-string parser. The strings
 * are the DT_MACHINE_START names in the vendor kernel, and the S3L cpuinfo is
 * the one from issue #266. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hal/ambarella.h"

int chip_generation;
char chip_name[128];
const char *getchipfamily() { return chip_name; }

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static bool parse(const char *cpuinfo, char *out, size_t outlen) {
    char path[] = "/tmp/ambarella_test.XXXXXX";
    int fd = mkstemp(path);

    CHECK(fd >= 0);
    CHECK(write(fd, cpuinfo, strlen(cpuinfo)) == (ssize_t)strlen(cpuinfo));
    close(fd);
    bool res = ambarella_machine_name(path, out, outlen);
    unlink(path);
    return res;
}

int main() {
    char buf[128];

    CHECK(parse("processor\t: 0\n"
                "Hardware\t: Ambarella S3L (Flattened Device Tree)\n"
                "Revision\t: 0000\n",
                buf, sizeof(buf)));
    CHECK(!strcmp(buf, "S3L"));

    CHECK(parse("Hardware\t: Ambarella S2L (Flattened Device Tree)\n", buf,
                sizeof(buf)));
    CHECK(!strcmp(buf, "S2L"));

    CHECK(parse("Hardware\t: Ambarella A5S (Flattened Device Tree)\n", buf,
                sizeof(buf)));
    CHECK(!strcmp(buf, "A5S"));

    CHECK(!parse("Hardware\t: SStar Soc (Flattened Device Tree)\n", buf,
                 sizeof(buf)));
    CHECK(!parse("model name\t: ARMv7 Processor rev 1 (v7l)\n", buf,
                 sizeof(buf)));

    /* Nothing may land in the buffer, or just before it. */
    char guarded[3] = {'x', 'y', 'z'};
    CHECK(!parse("Hardware\t: Ambarella S3L (Flattened Device Tree)\n",
                 guarded + 1, 0));
    CHECK(guarded[0] == 'x' && guarded[1] == 'y' && guarded[2] == 'z');

    /* The reporter's board: 512 MiB, with 392 MiB of it the DSP's. */
    uint32_t start = 0, size = 0;
    CHECK(ambarella_iavmem_from_log(
        "Ambarella:    DRAMC = 0xdffe0000[0xef000000],0x00020000 0\n"
        "Ambarella:   IAVMEM = 0x07800000[          ],0x18800000\n"
        "Ambarella:    FBMEM = 0x00000000[          ],0x00000000\n",
        &start, &size));
    CHECK(start == 0x07800000 && size == 0x18800000);
    CHECK(!ambarella_iavmem_from_log("Linux version 3.10.73\n", &start, &size));
    /* Cut off inside the size: 0x1880 must not be taken for the answer. */
    CHECK(!ambarella_iavmem_from_log(
        "Ambarella:   IAVMEM = 0x07800000[          ],0x1880", &start, &size));
    CHECK(!ambarella_iavmem_from_log(
        "Ambarella:   IAVMEM = 0x07800000[          ],0x18800000", &start,
        &size));

    char path[] = "/tmp/ambarella_test.XXXXXX";
    int fd = mkstemp(path);
    const unsigned char reg[] = {0x0e, 0x40, 0x00, 0x00,
                                 0x11, 0xc0, 0x00, 0x00};
    CHECK(fd >= 0);
    CHECK(write(fd, reg, sizeof(reg)) == (ssize_t)sizeof(reg));
    close(fd);
    CHECK(ambarella_iavmem_from_dt(path, &start, &size));
    CHECK(start == 0x0e400000 && size == 0x11c00000);
    CHECK(truncate(path, 4) == 0);
    CHECK(!ambarella_iavmem_from_dt(path, &start, &size));
    unlink(path);
    CHECK(!ambarella_iavmem_from_dt(path, &start, &size));

    if (failures)
        return EXIT_FAILURE;
    puts("ambarella_test: all cases passed");
    return EXIT_SUCCESS;
}
