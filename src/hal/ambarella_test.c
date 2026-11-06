/* Hardware-free cover for the Ambarella machine-string parser. The strings
 * are the DT_MACHINE_START names in the vendor kernel, and the S3L cpuinfo is
 * the one from issue #266. */
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

    if (failures)
        return EXIT_FAILURE;
    puts("ambarella_test: all cases passed");
    return EXIT_SUCCESS;
}
