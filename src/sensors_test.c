/* A behavioural fingerprint of the i2c sensor probes, taken off a mock bus.
 *
 * The probes decide which driver every camera loads, and almost none of the
 * parts they name are on anyone's desk -- the lab covers Sony and SmartSens
 * and nothing else. So the guard against a refactor changing an answer cannot
 * be hardware, and it cannot be a table written out by hand either: that is
 * transcribed from the same source the refactor edits, and an error copies
 * straight across.
 *
 * Instead this sweeps every value the ID registers can hold, 0x0000 to 0xffff,
 * through the real probe, and prints each one that yields a name. The output
 * is committed as sensors_golden.txt. Regenerate it with
 *
 *     ./build/sensors_test --dump > src/sensors_golden.txt
 *
 * and a diff of that file is exactly the set of parts whose identification
 * changed. An empty diff is the whole point: it says a rewrite named every
 * sensor the old code named, and named nothing new.
 *
 * CHECK rather than assert(), matching reginfo_test.c and longse_test.c: the
 * release flags carry -DNDEBUG, which compiles assert() out entirely. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The probes are static, and driving them through getsensorid() would mean
 * mocking /dev/i2c-N and the bus sweep as well. Including the translation
 * unit reaches them directly and keeps the mock to the register read. */
#include "sensors.c"

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                        \
        }                                                                      \
    } while (0)

/* ---- the mock bus ------------------------------------------------------ */

/* A scripted device. Registers not in the map read as -1, which is what a
 * probe sees when a slave does not answer, and every probe already has to
 * cope with that. Individual sweeps override the default where a family
 * needs unmapped registers to read as something else. */
#define MAX_REGS 16
static struct {
    uint32_t reg;
    int val;
} mock_regs[MAX_REGS];
static int mock_nregs;
static int mock_default = -1;
static unsigned char mock_addr = 0x60;

static void mock_clear(void) {
    mock_nregs = 0;
    mock_default = -1;
}

static void mock_set(uint32_t reg, int val) {
    CHECK(mock_nregs < MAX_REGS);
    if (mock_nregs >= MAX_REGS)
        return;
    mock_regs[mock_nregs].reg = reg;
    mock_regs[mock_nregs].val = val;
    mock_nregs++;
}

static int mock_read(int fd, unsigned char addr, uint32_t reg,
                     unsigned int reg_width, unsigned int data_width) {
    (void)fd;
    (void)reg_width;
    (void)data_width;

    if (addr != mock_addr)
        return -1;
    for (int i = 0; i < mock_nregs; i++)
        if (mock_regs[i].reg == reg)
            return mock_regs[i].val;
    return mock_default;
}

static int mock_write(int fd, unsigned char addr, uint32_t reg,
                      unsigned int reg_width, uint32_t data,
                      unsigned int data_width) {
    (void)fd;
    (void)addr;
    (void)reg;
    (void)reg_width;
    (void)data;
    (void)data_width;

    return 0;
}

static int mock_change_addr(int fd, unsigned char addr) {
    (void)fd;

    return addr == mock_addr ? 0 : -1;
}

/* sensors.c reaches for these two in detect_smartsens_sensor(), where a pair
 * of IDs name a different part depending on the SoC. chipid.c would drag
 * every vendor HAL in behind it, so the vendor is a knob here instead -- which
 * is also the only way to exercise both sides of those two branches. */
static const char *mock_vendor = "HiSilicon";
const char *getchipvendor() { return mock_vendor; }
const char *getchipname() { return "mock"; }

/* ---- sweeping one family ----------------------------------------------- */

typedef void (*set_id_fn)(uint32_t id);

struct family {
    const char *name;
    int (*probe)(sensor_ctx_t *ctx, int fd, unsigned char i2c_addr);
    set_id_fn set_id;
    int dflt; /* what an unmapped register reads as, for this family */
};

static void id_smartsens(uint32_t id) {
    mock_set(0x3107, id >> 8);
    mock_set(0x3108, id & 0xff);
}
static void id_soi(uint32_t id) {
    mock_set(0x0a, id >> 8);
    mock_set(0x0b, id & 0xff);
}
static void id_onsemi(uint32_t id) { mock_set(0x3000, id); }
static void id_omni(uint32_t id) {
    mock_set(0x300a, id >> 8);
    mock_set(0x300b, id & 0xff);
}
static void id_galaxycore(uint32_t id) {
    mock_set(0x3f0, id >> 8);
    mock_set(0x3f1, id & 0xff);
}
static void id_superpix(uint32_t id) {
    mock_set(0xfd, 0);
    mock_set(0x02, id >> 8);
    mock_set(0x03, id & 0xff);
}
static void id_techpoint(uint32_t id) {
    mock_set(0xfe, id >> 8);
    mock_set(0xff, id & 0xff);
}
static void id_imagedesign(uint32_t id) {
    mock_set(0x3000, id >> 8);
    mock_set(0x3001, id & 0xff);
}
static void id_visemi(uint32_t id) { mock_set(0x3000, id); }
static void id_cvsens(uint32_t id) {
    mock_set(0x3003, id >> 8);
    mock_set(0x3002, id & 0xff);
}

static const struct family families[] = {
    {"smartsens", detect_smartsens_sensor, id_smartsens, -1},
    {"soi", detect_soi_sensor, id_soi, -1},
    {"onsemi", detect_onsemi_sensor, id_onsemi, -1},
    {"omni", detect_omni_sensor, id_omni, -1},
    {"galaxycore", detect_galaxycore_sensor, id_galaxycore, -1},
    {"superpix", detect_superpix_sensor, id_superpix, -1},
    {"imagedesign", detect_imagedesign_sensor, id_imagedesign, -1},
    {"visemi", detect_visemi_sensor, id_visemi, -1},
    {"cvsens", detect_cvsens_sensor, id_cvsens, -1},
};

/* Run one probe against one ID. Returns the name it produced, or NULL if it
 * declined. The context is scrubbed first: getsensorid() hands the probes an
 * uninitialised one, so anything the probe does not write is a reading of the
 * previous call, and that is exactly the kind of bug this should surface. */
static const char *probe_once(const struct family *f, uint32_t id) {
    static sensor_ctx_t ctx;

    memset(&ctx, 0xa5, sizeof(ctx));
    ctx.sensor_id[0] = '\0';
    ctx.vendor[0] = '\0';
    ctx.reg_width = 2;
    ctx.data_width = 1;

    mock_clear();
    mock_default = f->dflt;
    f->set_id(id);

    if (!f->probe(&ctx, 0, mock_addr))
        return NULL;

    /* A probe that reports success has to have written a name. Reading back
     * the scrub pattern means it did not -- two SmartSens arms used to do
     * exactly that. */
    ctx.sensor_id[sizeof(ctx.sensor_id) - 1] = '\0';
    if (!ctx.sensor_id[0] || strchr(ctx.sensor_id, (char)0xa5))
        return "<uninitialised>";
    return ctx.sensor_id;
}

static void sweep(const struct family *f, FILE *out) {
    for (uint32_t id = 0; id <= 0xffff; id++) {
        const char *name = probe_once(f, id);
        if (name)
            fprintf(out, "%s %04x %s\n", f->name, id, name);
    }
}

/* ---- comparing against the committed fingerprint ----------------------- */

static void compare(FILE *golden) {
    char want[256];

    for (size_t i = 0; i < ARRCNT(families); i++) {
        const struct family *f = &families[i];
        for (uint32_t id = 0; id <= 0xffff; id++) {
            const char *name = probe_once(f, id);
            if (!name)
                continue;

            char got[256];
            snprintf(got, sizeof(got), "%s %04x %s\n", f->name, id, name);
            if (!fgets(want, sizeof(want), golden)) {
                fprintf(stderr, "  FAIL extra line, not in the fingerprint: %s",
                        got);
                failures++;
                return;
            }
            if (strcmp(got, want)) {
                fprintf(stderr, "  FAIL wanted: %s        got: %s", want, got);
                failures++;
            }
        }
    }
    if (fgets(want, sizeof(want), golden)) {
        fprintf(stderr, "  FAIL a sensor stopped being identified: %s", want);
        failures++;
    }
}

/* ---- the parts the sweep cannot reach ---------------------------------- */

/* Sony has no ID register: detect_sony_sensor() is a decision tree over
 * initialisation defaults, so there is no value to sweep. These are the two
 * disambiguations that field reports paid for, and they are checked by hand
 * so a refactor cannot quietly undo them. */
static void check_sony(void) {
    sensor_ctx_t ctx;

    /* #157: 0x3057 is host-writable, so an IMX335 that has been through a WDR
     * crop reads 0x06 there and used to come back IMX347. The OB cropping
     * defaults are what separate them. */
    memset(&ctx, 0, sizeof(ctx));
    mock_clear();
    mock_set(0x3057, 0x06);
    mock_set(0x3072, 0x28);
    mock_set(0x3074, 0xb0);
    mock_set(0x316a, 0x7e);
    mock_set(0x3b00, 0x2e);
    mock_set(0x300b, 0x00);
    sensor_read_register = mock_read;
    CHECK(detect_sony_sensor(&ctx, 0, mock_addr));
    CHECK(!strcmp(ctx.sensor_id, "IMX335"));
    if (strcmp(ctx.sensor_id, "IMX335"))
        fprintf(stderr, "    (#157 IMX335 vs IMX347: got \"%s\")\n",
                ctx.sensor_id);

    memset(&ctx, 0, sizeof(ctx));
    mock_clear();
    mock_set(0x3057, 0x06);
    mock_set(0x3072, 0x14);
    mock_set(0x3074, 0x3c);
    CHECK(detect_sony_sensor(&ctx, 0, mock_addr));
    CHECK(!strcmp(ctx.sensor_id, "IMX347"));

    /* #179: an IMX415 brought up with the IMX335 driver reads 0x7e at 0x316a
     * just as an IMX335 does, and the misdetection latches until it loses
     * power. 0x3b00 and 0x300b are what rule it out. */
    memset(&ctx, 0, sizeof(ctx));
    mock_clear();
    mock_set(0x316a, 0x7e);
    mock_set(0x3b00, 0x2e);
    mock_set(0x300b, 0xa0);
    CHECK(detect_sony_sensor(&ctx, 0, mock_addr));
    CHECK(!strcmp(ctx.sensor_id, "IMX415"));
    if (strcmp(ctx.sensor_id, "IMX415"))
        fprintf(stderr, "    (#179 IMX415 vs IMX335: got \"%s\")\n",
                ctx.sensor_id);

    /* A failed read must not pass for "not an IMX415": that is what re-armed
     * the latch. -1 at either register means give up, not guess. */
    memset(&ctx, 0, sizeof(ctx));
    mock_clear();
    mock_set(0x316a, 0x7e);
    mock_set(0x3b00, -1);
    CHECK(!detect_sony_sensor(&ctx, 0, mock_addr));
}

/* TechPoint is a rule, not a table: it whitelists nothing and turns any
 * non-zero ID into TP%04x. Sweeping it would put 65535 near-identical lines in
 * the fingerprint and bury every other family, so the rule is checked here
 * instead -- including the zero that has to be refused, which is the only
 * thing separating "an ADC answered" from "nothing on the bus". */
static void check_techpoint(void) {
    static const struct family techpoint = {"techpoint", detect_techpoint_adc,
                                            id_techpoint, -1};
    static const uint32_t ids[] = {0x0001, 0x2802, 0x2855, 0x9930, 0xffff};

    for (size_t i = 0; i < ARRCNT(ids); i++) {
        char want[16];
        snprintf(want, sizeof(want), "TP%04x", ids[i]);
        const char *got = probe_once(&techpoint, ids[i]);
        CHECK(got && !strcmp(got, want));
        if (!got || strcmp(got, want))
            fprintf(stderr,
                    "    (techpoint %#06x: wanted \"%s\", got \"%s\")\n",
                    ids[i], want, got ? got : "(none)");
    }
    CHECK(probe_once(&techpoint, 0) == NULL);
}

/* The two IDs SmartSens gives to different parts depending on the SoC. */
static void check_vendor_dependent(void) {
    static const struct {
        uint32_t id;
        const char *vendor;
        const char *want;
    } cases[] = {
        {0xcb17, "Ingenic", "SC2332"},
        {0xcb17, "HiSilicon", "SC2232"},
        {0xcb1c, "SigmaStar", "SC200AI"},
        {0xcb1c, "HiSilicon", "SC337H"},
    };
    static const struct family smartsens = {
        "smartsens", detect_smartsens_sensor, id_smartsens, -1};

    for (size_t i = 0; i < ARRCNT(cases); i++) {
        mock_vendor = cases[i].vendor;
        const char *got = probe_once(&smartsens, cases[i].id);
        CHECK(got && !strcmp(got, cases[i].want));
        if (!got || strcmp(got, cases[i].want))
            fprintf(stderr, "    (%#06x on %s: wanted \"%s\", got \"%s\")\n",
                    cases[i].id, cases[i].vendor, cases[i].want,
                    got ? got : "(none)");
    }
    mock_vendor = "HiSilicon";
}

int main(int argc, char **argv) {
    i2c_read_register = mock_read;
    i2c_write_register = mock_write;
    i2c_change_addr = mock_change_addr;
    sensor_read_register = mock_read;
    sensor_write_register = mock_write;

    if (argc > 1 && !strcmp(argv[1], "--dump")) {
        for (size_t i = 0; i < ARRCNT(families); i++)
            sweep(&families[i], stdout);
        return 0;
    }

    const char *path = argc > 1 ? argv[1] : "src/sensors_golden.txt";
    FILE *golden = fopen(path, "r");
    if (!golden) {
        fprintf(stderr, "cannot read the fingerprint at %s\n", path);
        return 2;
    }
    compare(golden);
    fclose(golden);

    check_sony();
    check_techpoint();
    check_vendor_dependent();

    if (failures)
        fprintf(stderr, "sensors_test: %d failure(s)\n", failures);
    else
        printf("sensors_test: sensor identification unchanged\n");
    return failures ? 1 : 0;
}
