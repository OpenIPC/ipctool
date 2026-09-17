/* Hardware-free cover for the Anyka parsing. Nobody on the project has one of
 * these cameras, so this is the only check the HAL gets: the inputs below are
 * transcribed from the Victure PC420 dumps in issue #134, from vendor kernel
 * sources (arch/arm/mach-ak39, arch/arm/mach-anycloud) and from boot logs of
 * other Anyka cameras.
 *
 * CHECK rather than assert(), matching longse_test.c: the release flags carry
 * -DNDEBUG, which compiles assert() out entirely and would leave this file
 * passing whatever the code returned. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hal/anyka.h"

/* chipid.c supplies these on a real build. Stubbing them rather than linking
 * it is what keeps every other vendor HAL out of this test. */
int chip_generation;
char chip_name[128];
const char *getchipfamily() { return "ak3918ev300"; }

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static char fixture_path[64];

/* line_from_file() only takes a path, so a fixture has to be a real file. */
static const char *fixture(const char *body) {
    static int seq;

    snprintf(fixture_path, sizeof(fixture_path), "/tmp/anyka_test.%d.%d",
             (int)getpid(), seq++);
    FILE *f = fopen(fixture_path, "w");
    if (!f) {
        fprintf(stderr, "  FAIL cannot write %s\n", fixture_path);
        failures++;
        return "/nonexistent";
    }
    fputs(body, f);
    fclose(f);
    return fixture_path;
}

static void expect_machine(const char *cpuinfo, const char *want) {
    char got[128];
    const char *path = fixture(cpuinfo);
    bool ok = anyka_machine_name(path, got, sizeof(got));

    unlink(path);
    if (!want) {
        CHECK(!ok);
        if (ok)
            fprintf(stderr, "    (wanted no match, got \"%s\")\n", got);
        return;
    }
    CHECK(ok && !strcmp(got, want));
    if (!ok || strcmp(got, want))
        fprintf(stderr, "    (wanted \"%s\", got \"%s\")\n", want,
                ok ? got : "(no match)");
}

static void expect_media(const char *cmdline, const char *iomem,
                         unsigned long want) {
    char cmd_path[64];
    const char *path = fixture(cmdline);

    snprintf(cmd_path, sizeof(cmd_path), "%s", path);
    path = fixture(iomem);

    unsigned long got = anyka_media_mem(cmd_path, path);

    unlink(cmd_path);
    unlink(path);
    CHECK(got == want);
    if (got != want)
        fprintf(stderr, "    (wanted %lu kB, got %lu kB)\n", want, got);
}

/* The Hardware line as each kernel's MACHINE_START spells it. The tab and the
 * trailing lines are there because that is what /proc/cpuinfo looks like and
 * the regex has to survive both. */
static void machine_names(void) {
    /* Victure PC420, issue #134. */
    expect_machine("Processor\t: ARM926EJ-S rev 5 (v5l)\n"
                   "Hardware\t: CLOUD39EV3_AK3918EV300_MNBD\n"
                   "Revision\t: 0000\n",
                   "AK3918EV300");

    /* mach-ak39ev330.c, both of its machines. */
    expect_machine("Hardware\t: Cloud39EV2_AK3918E80PIN_MNBD\n",
                   "AK3918E80PIN");
    expect_machine("Hardware\t: AK39EV330\n", "AK39EV330");

    /* Lamobo-D1 and the other Aimer39 reference boards. */
    expect_machine("Hardware\t: Aimer39_AK3918_MB_V1.0.0\n", "AK3918");
    expect_machine("Hardware\t: Aimer39_AK3916_MB_V1.0.0\n", "AK3916");

    /* AnyCloud37 parts are the same register map and the same shape of name. */
    expect_machine("Hardware\t: AK37D\n", "AK37D");

    /* Not Anyka: every other SoC in the table has to fall through to its own
     * detector rather than be claimed here. */
    expect_machine("Hardware\t: Hisilicon Hi3516DV300\n", NULL);
    expect_machine("Hardware\t: sstar soc\n", NULL);
    expect_machine("Hardware\t: Ingenic-T31\n", NULL);
    expect_machine("Hardware\t: Generic DT based system\n", NULL);
    expect_machine("Processor\t: ARM926EJ-S rev 5 (v5l)\n", NULL);

    /* "AK39xx" is the part, "SDK3910 Board" is not: the digits have to follow
     * an AK for this to be a claim about the silicon. */
    expect_machine("Hardware\t: SDK3910 Board\n", NULL);
}

static void chip_ids(void) {
    /* Every value a vendor kernel compares AK_CPU_ID against. The first two
     * are one generation under two spellings -- the register cannot tell an
     * AK3916 from an AK3918, which is why the machine string names the part
     * and this only names the family. */
    CHECK(anyka_generation(0x20120100) == AK39_EV2);
    CHECK(anyka_generation(0x20150200) == AK39_EV2);
    CHECK(anyka_generation(0x20160100) == AK39_EV3);
    CHECK(anyka_generation(0x20160101) == AK39_EV330);
    CHECK(anyka_generation(0x20170200) == AK37_D);

    /* The later parts match on bits 31:8 and leave the low byte to vary. */
    CHECK(anyka_generation(0x20190200) == AK37_E);
    CHECK(anyka_generation(0x201902ff) == AK37_E);
    CHECK(anyka_generation(0x53533500) == AK39_AV100);
    CHECK(anyka_generation(0x53533542) == AK39_AV100);
    CHECK(anyka_generation(0x53543400) == AK39_EV300L);

    /* A shifted row must not swallow a raw one, or an EV300 would report as
     * whatever 0x201601.. happened to be listed. */
    CHECK(anyka_generation(0x20160102) == 0);
    CHECK(anyka_generation(0x20120101) == 0);
    CHECK(anyka_generation(0x00000000) == 0);
    CHECK(anyka_generation(0xffffffff) == 0);
}

static void media_mem(void) {
    /* The PC420 again: 64 MB of DDR, of which the kernel was handed
     * 0x81b00000-0x83ffffff = 37 MB, so 27 MB went to the ISP and encoder. */
    expect_media("console=ttySAK0,115200n8 root=/dev/mtdblock4 "
                 "rootfstype=squashfs init=/sbin/init mem=64M memsize=64M\n",
                 "20150000-20150100 : i2c-ak39\n"
                 "81b00000-83ffffff : System RAM\n"
                 "  81b08000-81ea7fff : Kernel code\n",
                 27 * 1024);

    /* A board that reserves nothing reports nothing, rather than a zero-sized
     * media section or an underflowed one. */
    expect_media("memsize=64M\n", "80000000-83ffffff : System RAM\n", 0);
    expect_media("memsize=32M\n", "80000000-83ffffff : System RAM\n", 0);

    /* Either input missing is a "do not know", not a guess. */
    expect_media("mem=64M\n", "81b00000-83ffffff : System RAM\n", 0);
    expect_media("memsize=64M\n", "81b00000-83ffffff : Something Else\n", 0);
    expect_media("memsize=64M\n", "", 0);

    /* The kernel line has to be the System RAM one, not the first range in
     * the file -- /proc/iomem leads with the peripherals. */
    expect_media("memsize=128M\n",
                 "08000000-0800ffff : akpcm_AnalogCtrlRegs\n"
                 "20200000-202003ff : usb-host\n"
                 "84000000-87ffffff : System RAM\n",
                 64 * 1024);
}

int main(void) {
    machine_names();
    chip_ids();
    media_mem();

    if (failures) {
        fprintf(stderr, "anyka_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("anyka_test: all cases passed\n");
    return 0;
}
