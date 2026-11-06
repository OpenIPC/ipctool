/* Hardware-free cover for the Ambarella machine-string parser. The strings
 * are the DT_MACHINE_START names in the vendor kernel, and the S3L cpuinfo is
 * the one from issue #266. */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hal/ambarella.h"
#include "hal/common.h"

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

    /* ram.c rounds the total up to a power of two; on this board the total
     * already is one and must stay 512M, not become 1024M. */
    CHECK(rounded_num((0x07800000u + 0x18800000u) / 1024 / 1024) == 512);
    CHECK(rounded_num(112) == 128); /* a MemTotal of 114940 kB */
    CHECK(rounded_num(513) == 1024);
    CHECK(rounded_num(1) == 1);

    /* Two boards.  On the DS-2CD2423G0-I /iavmem is stale: the DT says the
     * DSP starts at 64 MiB and Linux runs to 112 MiB.  On the IP4M-1026E the
     * two agree, Linux ending exactly where the DSP starts. */
    {
        char iomem[] = "/tmp/ambarella_test.XXXXXX";
        const char *hik = "00200000-06ffffff : System RAM\n"
                          "  00208000-00719667 : Kernel code\n";
        const char *dahua = "00200000-077fffff : System RAM\n";
        /* Split, the higher range last, and an indented line that must not
         * count however high it goes */
        const char *split = "00200000-03ffffff : System RAM\n"
                            "  00208000-00719667 : Kernel code\n"
                            "04100000-06ffffff : System RAM\n"
                            "  0ff00000-0fffffff : System RAM\n";
        const char *none = "e8005000-e8005fff : uart\n";
        const uint64_t top = 0x10000000;
        int ifd = mkstemp(iomem);

        CHECK(ifd >= 0);
        close(ifd);
#define IOMEM(text)                                                            \
    do {                                                                       \
        int w = open(iomem, O_WRONLY | O_TRUNC);                               \
        CHECK(w >= 0);                                                         \
        CHECK(write(w, text, strlen(text)) == (ssize_t)strlen(text));          \
        close(w);                                                              \
    } while (0)
        IOMEM(hik);
        CHECK(ambarella_media_kb(iomem, top) == 144 * 1024);
        IOMEM(dahua);
        CHECK(ambarella_media_kb(iomem, 0x20000000) == 392 * 1024);
        IOMEM(split);
        CHECK(ambarella_media_kb(iomem, top) == 144 * 1024);

        /* Nothing to measure from, or Linux past the end: unknown */
        IOMEM(none);
        CHECK(ambarella_media_kb(iomem, top) == 0);
        IOMEM(hik);
        CHECK(ambarella_media_kb(iomem, 0x07000000) == 0);
        unlink(iomem);
        CHECK(ambarella_media_kb(iomem, top) == 0);
#undef IOMEM
    }

    {
        char iav[] = "/tmp/ambarella_test.XXXXXX";
        const char *text = "AMBARELLA_CHIP=S3Lm\ndsp_op_mode: 1\n";
        char family[16];
        int ifd = mkstemp(iav);

        CHECK(ifd >= 0);
        CHECK(write(ifd, text, strlen(text)) == (ssize_t)strlen(text));
        close(ifd);
        CHECK(ambarella_family(iav, family, sizeof(family)));
        CHECK(!strcmp(family, "S3Lm"));
        unlink(iav);
        CHECK(!ambarella_family(iav, family, sizeof(family)));
    }

    if (failures)
        return EXIT_FAILURE;
    puts("ambarella_test: all cases passed");
    return EXIT_SUCCESS;
}
