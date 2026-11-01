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
/* Read all project/system declarations before the synthetic architecture
 * selection, especially the native Linux ioctl definitions. */
#include "chipid.h"
#include "hal/common.h"
#include "sensors.h"

static bool fixture_line_from_file(const char *path, const char *re,
                                   char *out, size_t len);
#define line_from_file fixture_line_from_file
/* Select both the HAL and routing table's architecture on native CI. System
 * headers are read above before changing these macros. The default is the
 * MIPS fixture; two additional test targets cover the existing ARM routes.
 * These selections affect only this test translation unit, not production.
 * A real MIPS build/QEMU run remains a separate check. */
#pragma push_macro("__arm__")
#pragma push_macro("__aarch64__")
#pragma push_macro("__mips__")
#pragma push_macro("__mips")
#pragma push_macro("mips")
#undef __arm__
#undef __aarch64__
#undef __mips__
#undef __mips
#undef mips
#if defined(NOVATEK_TEST_ARM)
#define __arm__ 1
#elif defined(NOVATEK_TEST_AARCH64)
#define __aarch64__ 1
#else
#define __mips__ 1
#endif
#include "novatek.c"
#include "../chipid.c"
#pragma pop_macro("mips")
#pragma pop_macro("__mips")
#pragma pop_macro("__mips__")
#pragma pop_macro("__aarch64__")
#pragma pop_macro("__arm__")
#undef line_from_file
#include "sensors.h"

static int failures;
static int hardware_opens;
static int printk_disables;
static int arm_chip_id_reads;
static int profile_reads;
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
    if (!strcmp(path, "/proc/nvt_info/nvt_pinmux/chip_id")) {
        arm_chip_id_reads++;
        name = "chip_id";
    }
    else if (!strcmp(path, "/proc/device-tree/model"))
        name = "model";
    else if (!strcmp(path, "/etc/profile_prjcfg")) {
        profile_reads++;
        name = "profile";
    }
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

#if !defined(NOVATEK_TEST_ARM) && !defined(NOVATEK_TEST_AARCH64)
static void mips_dispatch(void) {
    fixture("chip_id", NULL);
    fixture("model", "Novatek NA51023 evaluation board");
    fixture("profile", "export CHIP=\"98513\"\n");
    fixture("cpuinfo", "system type\t: NVT NA51023\n"
                       "machine\t\t: Novatek NA51023 evaluation board\n"
                       "cpu model\t: MIPS 24KEc V5.5\n");
    chip_generation = 0;
    *sysid = '\0';
    arm_chip_id_reads = 0;
    const char *name = getchipname();
    CHECK(name && !strcmp(name, "nt98513"));
    if (!name)
        return;
    CHECK(!strcmp(getchipvendor(), "Novatek"));
    CHECK(!strcmp(getchipfamily(), "nt9851x"));
    CHECK(possible_i2c_addrs == NULL);
    CHECK(arm_chip_id_reads == 0);

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

    /* This architecture has no verified ARM chip-ID proc ABI. Its detector
     * must use the MIPS model even if an unrelated ARM ID file is present. */
    fixture("chip_id", "7021\n");
    chip_generation = 0;
    *sysid = '\0';
    arm_chip_id_reads = 0;
    name = getchipname();
    CHECK(name && !strcmp(name, "nt98513"));
    CHECK(arm_chip_id_reads == 0);
    CHECK(possible_i2c_addrs == NULL);
}

static void mips_reject_unsupported(const char *id, const char *model) {
    fixture("chip_id", id);
    fixture("model", model);
    fixture("profile", "export CHIP=\"98513\"\n");
    fixture("cpuinfo", "machine\t: Novatek\n");
    chip_generation = 0;
    *sysid = '\0';
    possible_i2c_addrs = NULL;
    arm_chip_id_reads = 0;
    CHECK(getchipname() == NULL);
    CHECK(chip_generation == 0);
    CHECK(possible_i2c_addrs == NULL);
    CHECK(open_i2c_sensor_fd != novatek_open_i2c_fd);
    CHECK(arm_chip_id_reads == 0);
}
#endif

int main(void) {
    if (!mkdtemp(fixture_dir)) {
        perror("mkdtemp");
        return 2;
    }
    const char *model = "Novatek NA51023 evaluation board";
    const char *profile = "export PLATFORM=\"Linux\"\nexport CHIP=\"98513\"\n";
#if defined(NOVATEK_TEST_ARM) || defined(NOVATEK_TEST_AARCH64)
    /* An ARM build must not refine the MIPS-only model from its profile or
     * install the MIPS rejecting HAL. Keep the existing ARM ID/model route. */
    profile_reads = 0;
    expect_name(NULL, model, profile, "NA51023", false);
    CHECK(chip_generation != CHIP_NA51023);
    expect_name(NULL, "Novatek NA51023", profile, "NA51023", false);
    /* These generic ARM substring matches are inherited behavior. Strict
     * complete-token rejection is a guarantee of the new MIPS route only. */
    expect_name(NULL, "Novatek NA51023A evaluation board", profile, "NA51023", false);
    expect_name(NULL, "OtherNovatek NA51023 evaluation board", profile, "NA51023", false);
    expect_name(NULL, "Novatek NA51055 evaluation board", profile, "NA51055", false);
    expect_name("7021\n", NULL, profile, "NT98566", false);
    expect_name("5021\n", NULL, NULL, "NT98528", false);
    expect_name("8B20\n", NULL, NULL, "NT98332G", false);
    expect_name("4821\n", NULL, NULL, "NA51055", false);
    CHECK(profile_reads == 0);
#else
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
    mips_reject_unsupported(NULL, "Novatek NA510230 evaluation board");
    mips_reject_unsupported(NULL, "Novatek NA51055 evaluation board");
    mips_reject_unsupported("7021\n", NULL);
    mips_reject_unsupported("7021\n", "Novatek NA51055 evaluation board");
#endif
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
