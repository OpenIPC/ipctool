/* Hardware-free identity checks. The NA51023 model and CHIP profile below
 * come from an NT98513 firmware dump; they are not a live /proc capture.
 * This test redirects file paths, but uses the real line_from_file parser
 * and detector. It must not acquire /dev/mem, an I2C bus or a camera. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "tools.h"

static bool fixture_line_from_file(const char *path, const char *re,
                                   char *out, size_t len);
#define line_from_file fixture_line_from_file
#include "novatek.c"
/* Select chipid.c's MIPS routing table on native CI too. All its system and
 * project headers have already been read above; architecture macros change
 * only while this platform-dispatch source is included. A real MIPS build
 * and QEMU run remain separate from this conditional-routing check. */
#pragma push_macro("__arm__")
#pragma push_macro("__aarch64__")
#pragma push_macro("__mips__")
#undef __arm__
#undef __aarch64__
#undef __mips__
#define __mips__ 1
#include "../chipid.c"
#pragma pop_macro("__mips__")
#pragma pop_macro("__aarch64__")
#pragma pop_macro("__arm__")
#undef line_from_file
#include "sensors.h"

static int failures;
static int hardware_opens;
static int printk_disables;
static char fixture_dir[] = "/tmp/novatek_test.XXXXXX";

/* The test link wraps these functions. Catch even an attempted device open,
 * rather than silently relying on /dev/i2c-N being absent in CI. */
int __wrap_open(const char *path, int flags, ...) {
    (void)path;
    (void)flags;
    hardware_opens++;
    return -1;
}

void __wrap_disable_printk(void) { printk_disables++; }

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static bool fixture_line_from_file(const char *path, const char *re,
                                   char *out, size_t len) {
    const char *name = NULL;
    if (!strcmp(path, "/proc/nvt_info/nvt_pinmux/chip_id"))
        name = "chip_id";
    else if (!strcmp(path, "/proc/device-tree/model"))
        name = "model";
    else if (!strcmp(path, "/etc/profile_prjcfg"))
        name = "profile";
    else if (!strcmp(path, "/proc/cpuinfo"))
        name = "cpuinfo";
    else if (!strcmp(path, "/proc/iomem"))
        return false;
    if (!name) {
        CHECK(!"unexpected hardware file read");
        return false;
    }
    char file[256];
    snprintf(file, sizeof(file), "%s/%s", fixture_dir, name);
    return line_from_file(file, re, out, len);
}

static void fixture(const char *name, const char *body) {
    char file[256];
    snprintf(file, sizeof(file), "%s/%s", fixture_dir, name);
    if (!body) {
        unlink(file);
        return;
    }
    FILE *fp = fopen(file, "w");
    if (!fp) {
        perror(file);
        exit(2);
    }
    fputs(body, fp);
    fclose(fp);
}

static void expect_name(const char *id, const char *model,
                        const char *profile, const char *want, bool legacy) {
    fixture("chip_id", id);
    fixture("model", model);
    fixture("profile", profile);
    chip_generation = 0;
    strcpy(chip_name, "unset");
    bool ok = novatek_detect_cpu(chip_name);
    CHECK(ok == (want != NULL));
    if (!ok || !want)
        return;
    CHECK(!strcmp(chip_name, want));
    if (strcmp(chip_name, want))
        fprintf(stderr, "  wanted %s, got %s\n", want, chip_name);

    setup_hal_fallback();
    possible_i2c_addrs = NULL;
    novatek_setup_hal();
    /* Recognising an RTOS media platform must not enable the ARM HDAL/I2C
     * backend. ARM parts must keep their existing sensor probing. */
    CHECK((possible_i2c_addrs == NULL) == legacy);
}

static void mips_dispatch(void) {
    fixture("chip_id", NULL);
    fixture("model", "Novatek NA51023 evaluation board");
    fixture("profile", "export CHIP=\"98513\"\n");
    fixture("cpuinfo", "system type\t: NVT NA51023\n"
                       "machine\t\t: Novatek NA51023 evaluation board\n"
                       "cpu model\t: MIPS 24KEc V5.5\n");
    chip_generation = 0;
    *sysid = '\0';
    const char *name = getchipname();
    CHECK(name && !strcmp(name, "nt98513"));
    if (!name)
        return;
    CHECK(!strcmp(getchipvendor(), "Novatek"));
    CHECK(!strcmp(getchipfamily(), "nt9851x"));
    CHECK(possible_i2c_addrs == NULL);

    sensor_ctx_t sensor;
    hardware_opens = 0;
    printk_disables = 0;
    CHECK(hal_enable_sensor_clock == NULL);
    CHECK(!getsensorid(&sensor));
    CHECK(hardware_opens == 0);
    CHECK(printk_disables == 0);

    /* The dump gives us the device-tree model, not a live cpuinfo capture.
     * Do not make detection depend on the latter's formatting. */
    fixture("cpuinfo", "system type\t: NVT NA51023\n");
    *sysid = '\0';
    name = getchipname();
    CHECK(name && !strcmp(name, "nt98513"));
}

int main(void) {
    if (!mkdtemp(fixture_dir)) {
        perror("mkdtemp");
        return 2;
    }
    const char *model = "Novatek NA51023 evaluation board";
    const char *profile = "export PLATFORM=\"Linux\"\nexport CHIP=\"98513\"\n";
    expect_name(NULL, model, profile, "NT98513", true);
    expect_name(NULL, model, NULL, "NA51023", true);
    expect_name(NULL, model, "export CHIP=\"98515\"\n", "NA51023", true);
    expect_name(NULL, model, "# export CHIP=\"98513\"\n", "NA51023", true);
    expect_name(NULL, model, "export CHIP=\"985130\"\n", "NA51023", true);
    expect_name(NULL, model, "export CHIP=\"98513\"; touch /tmp/no\n",
                "NA51023", true);
    expect_name(NULL, NULL, profile, NULL, false);
    expect_name(NULL, "Other vendor NA51023", profile, NULL, false);
    expect_name(NULL, "OtherNovatek NA51023 evaluation board", profile,
                NULL, false);
    expect_name(NULL, "Novatek NA51023A evaluation board", profile,
                NULL, false);
    expect_name(NULL, "Novatek NA51023", profile, "NT98513", true);
    expect_name(NULL, "Novatek NA51023\tboard", profile, "NT98513", true);
    expect_name(NULL, "Novatek NA510230 evaluation board", profile,
                "NA510230", false);
    expect_name(NULL, "Novatek NA51055 evaluation board", profile,
                "NA51055", false);
    expect_name("7021\n", NULL, profile, "NT98566", false);
    expect_name("5021\n", NULL, NULL, "NT98528", false);
    expect_name("8B20\n", NULL, NULL, "NT98332G", false);
    expect_name("4821\n", NULL, NULL, "NA51055", false);
    mips_dispatch();
    fixture("chip_id", NULL);
    fixture("model", NULL);
    fixture("profile", NULL);
    fixture("cpuinfo", NULL);
    rmdir(fixture_dir);
    if (failures) {
        fprintf(stderr, "novatek_test: %d failure(s)\n", failures);
        return 1;
    }
    puts("novatek_test: all cases passed");
    return 0;
}
