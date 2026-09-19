/* The sensor-clock entitlement in hal_hisi.c, driven against a fake register
 * file.
 *
 * What this is here to stop coming back: a probe that finds the sensor clock
 * already running must leave it running, however many arm/restore pairs
 * getsensorid()'s bus sweep performs, and whatever an earlier probe in the
 * same process found. It did not. On a streaming hi3516ev300 + IMX335 the
 * cleanup at the end of a routine six-hourly probe wrote back a word saved
 * before the pipeline existed, gating the clock off and leaving the kernel
 * sensor driver unable to reach the sensor at all -- 908 consecutive
 * `i2c_master_send error, ret=-5` and a frozen picture (OpenIPC/firmware#2439).
 *
 * The invariant is asserted inside the fake's WRITE path rather than by
 * inspecting the register afterwards, so an illegal write is caught where it
 * happens and names itself.
 *
 * CHECK rather than assert(), because the release flags carry -DNDEBUG and
 * would compile an assert-based test into one that passes unconditionally.
 *
 * hal_hisi.c is included rather than linked: the state under test is three
 * file-statics that libipchw deliberately does not expose, and putting a seam
 * on mem_reg() to reach them would cost an indirect call in every ipcinfo on
 * every camera. src/sensors_test.c reaches its statics the same way. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hal/hisi/hal_hisi.c"

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                        \
        }                                                                      \
    } while (0)

/* ---- the fake register file -------------------------------------------- */

#define SLOTS 4

static struct {
    uint32_t addr;
    uint32_t val;
    bool used;
} REGS[SLOTS];

static int reads, writes;
static bool read_fails, write_fails;
static const char *illegal; /* set by the write checker */

/* What a legal undo looks like at the moment it is attempted: the register
 * still holds the word our own ungate left there, and we are putting back the
 * word that ungate found. Armed by the fake when it sees an ungating write,
 * spent by the undo. */
static struct {
    uint32_t addr, wrote, found;
    bool armed;
} UNDO[SLOTS]; /* per register: OT entitles two of them independently */

static int slot(uint32_t addr) {
    for (int i = 0; i < SLOTS; i++)
        if (REGS[i].used && REGS[i].addr == addr)
            return i;
    return -1;
}

static void seed(uint32_t addr, uint32_t val) {
    for (int i = 0; i < SLOTS; i++)
        if (!REGS[i].used) {
            REGS[i] = (typeof(REGS[0])){addr, val, true};
            return;
        }
}

/* "The consumer did this" -- the vendor SDK bringing a pipeline up, hours
 * after a probe. Bypasses the checker, because it is not us. */
static void poke(uint32_t addr, uint32_t val) {
    const int i = slot(addr);
    if (i >= 0)
        REGS[i].val = val;
}

static uint32_t peek(uint32_t addr) {
    const int i = slot(addr);
    return i >= 0 ? REGS[i].val : 0xdeadbeefu;
}

static void reset_counters(void) {
    reads = writes = 0;
    illegal = NULL;
}

/* The one rule, enforced where it is broken: never gate a running clock,
 * unless this is the undo of an ungate we ourselves performed and the
 * register still stands exactly as we left it. */
static void check_write(uint32_t addr, uint32_t val) {
    const uint32_t cur = peek(addr);
    const uint32_t cken = addr == EV300_PERI_CRG60_ADDR
        ? EV300_PERI_CRG60_SENSOR0_CKEN
        : CV610_PERI_CRG_SENSOR0_CKEN;

    if (!(cur & cken))
        return; /* clock already off; nothing to take away */
    if (val & cken)
        return; /* still on afterwards; not a gating write */

    for (int u = 0; u < SLOTS; u++)
        if (UNDO[u].armed && UNDO[u].addr == addr && cur == UNDO[u].wrote
            && val == UNDO[u].found) {
            UNDO[u].armed = false; /* the legal undo, spent */
            return;
        }
    illegal = "gated a running sensor clock that was not ours to gate";
}

bool mem_reg(uint32_t addr, uint32_t *data, enum REG_OPS op) {
    if (op == OP_READ) {
        reads++;
        if (read_fails)
            return false;
        const int i = slot(addr);
        if (i < 0)
            return false;
        *data = REGS[i].val;
        return true;
    }

    writes++;
    if (write_fails)
        return false;
    const int i = slot(addr);
    if (i < 0)
        return false;

    check_write(addr, *data);

    const uint32_t cken = addr == EV300_PERI_CRG60_ADDR
        ? EV300_PERI_CRG60_SENSOR0_CKEN
        : CV610_PERI_CRG_SENSOR0_CKEN;
    if (!(REGS[i].val & cken) && (*data & cken))
        UNDO[i] = (typeof(UNDO[0])){addr, *data, REGS[i].val, true};

    REGS[i].val = *data;
    return true;
}

/* The rest of hal_hisi.c's externals. None of them belongs in a unit test,
 * and disable_printk() is not optional: the real one silences the host's
 * kernel log if this ever runs as root. */
bool line_from_file(const char *filename, const char *re, char *buf, size_t n) {
    (void)filename;
    (void)re;
    (void)buf;
    (void)n;
    return false;
}
void disable_printk(void) {}
void restore_printk(void) {}

/* chipid.c's, normally. Naming it here is what keeps chipid.c and every other
 * vendor HAL out of the link. */
int chip_generation;

/* ---- helpers ----------------------------------------------------------- */

static void fresh(uint32_t addr, uint32_t val, int gen) {
    memset(REGS, 0, sizeof REGS);
    memset(&UNDO, 0, sizeof UNDO);
    memset(&v4_crg, 0, sizeof v4_crg);
    memset(ot_crg, 0, sizeof ot_crg);
    chip_generation = gen;
    seed(addr, val);
    if (addr == CV610_PERI_CRG8464_ADDR)
        seed(CV610_PERI_CRG8472_ADDR, val);
    reset_counters();
}

/* getsensorid()'s shape, arm for arm: bus 0 probed (sensors.c:1291 + the
 * cleanup at :1177), SPI armed with no cleanup of its own (:1299), then the
 * fall-through sweep over the remaining adapters (:1322). This mirrors the
 * real caller, which is the test's one piece of hand-copied knowledge. */
static void sweep(void) {
    hal_enable_sensor_clock();
    hal_cleanup();
    hal_enable_sensor_clock();
    for (int bus = 1; bus < 6; bus++) {
        hal_enable_sensor_clock();
        hal_cleanup();
    }
}

#define V4 EV300_PERI_CRG60_ADDR
#define OT CV610_PERI_CRG8464_ADDR

int main(void) {
    /* setup_hal_hisi() is what installs the two hooks, so driving the tests
     * through them proves that wiring as well. */

    /* A gated clock is ungated, exactly: clock on, reset clear, and the clock
     * select the board was using left alone. */
    fresh(V4, 0x10, HISI_V4);
    setup_hal_hisi();
    CHECK(peek(V4) == 0x11);
    CHECK(illegal == NULL);

    /* And put back when the probe that ungated it finishes. */
    fresh(V4, 0x10, HISI_V4);
    setup_hal_hisi();
    hal_cleanup();
    CHECK(peek(V4) == 0x10);
    CHECK(illegal == NULL);
    /* One arm entitles one undo; a second cleanup writes nothing. */
    int before = writes;
    hal_cleanup();
    CHECK(writes == before);

    /* A clock that was already running is not ours, however hard the sweep
     * arms and cleans up around it. */
    fresh(V4, 0x11, HISI_V4);
    setup_hal_hisi();
    reset_counters();
    sweep();
    CHECK(writes == 0);
    CHECK(peek(V4) == 0x11);
    CHECK(illegal == NULL);

    /* THE REGRESSION. A probe at boot finds the clock gated and ungates it;
     * the consumer then brings a pipeline up hours later; the next probe must
     * not hand the register back to the state the first one found. Before the
     * entitlement rules this wrote 0x11 -> 0x10 and took the camera off the
     * air. */
    fresh(V4, 0x10, HISI_V4);
    setup_hal_hisi();
    sweep();
    poke(V4, 0x11); /* the SDK starts streaming */
    reset_counters();
    sweep();
    CHECK(writes == 0);
    CHECK(peek(V4) == 0x11);
    CHECK(illegal == NULL);

    /* A register somebody else reprogrammed between our ungate and our undo
     * is theirs now. */
    fresh(V4, 0x10, HISI_V4);
    setup_hal_hisi();
    poke(V4, 0x1d); /* a different clock select, clock still on */
    reset_counters();
    hal_cleanup();
    CHECK(writes == 0);
    CHECK(peek(V4) == 0x1d);

    /* A read we could not make teaches nothing and entitles nothing. */
    fresh(V4, 0x10, HISI_V4);
    read_fails = true;
    setup_hal_hisi();
    read_fails = false;
    reset_counters();
    hal_cleanup();
    CHECK(writes == 0);
    CHECK(peek(V4) == 0x10);

    /* Nor does a write that failed. */
    fresh(V4, 0x10, HISI_V4);
    write_fails = true;
    setup_hal_hisi();
    write_fails = false;
    reset_counters();
    hal_cleanup();
    CHECK(writes == 0);

    /* OT carries two registers and entitles them separately. */
    fresh(OT, 0x00, HISI_OT);
    setup_hal_hisi();
    CHECK(peek(CV610_PERI_CRG8464_ADDR) & CV610_PERI_CRG_SENSOR0_CKEN);
    CHECK(peek(CV610_PERI_CRG8472_ADDR) & CV610_PERI_CRG_SENSOR0_CKEN);
    hal_cleanup();
    CHECK(peek(CV610_PERI_CRG8464_ADDR) == 0x00);
    CHECK(peek(CV610_PERI_CRG8472_ADDR) == 0x00);
    CHECK(illegal == NULL);

    /* Same regression, OT side. */
    fresh(OT, 0x00, HISI_OT);
    setup_hal_hisi();
    sweep();
    poke(CV610_PERI_CRG8464_ADDR, CV610_PERI_CRG_SENSOR0_CKEN);
    poke(CV610_PERI_CRG8472_ADDR, CV610_PERI_CRG_SENSOR0_CKEN);
    reset_counters();
    sweep();
    CHECK(writes == 0);
    CHECK(illegal == NULL);

    /* An OT register with the clock on but reset asserted is still unfit for
     * a probe, so it is taken over -- and given back. */
    fresh(OT, CV610_PERI_CRG_SENSOR0_CKEN | CV610_PERI_CRG_SENSOR0_SRST,
        HISI_OT);
    setup_hal_hisi();
    CHECK(!(peek(CV610_PERI_CRG8464_ADDR) & CV610_PERI_CRG_SENSOR0_SRST));
    hal_cleanup();
    CHECK(peek(CV610_PERI_CRG8464_ADDR)
        == (CV610_PERI_CRG_SENSOR0_CKEN | CV610_PERI_CRG_SENSOR0_SRST));

    /* V3 ungates without ever restoring, which is exactly why it cannot
     * suffer this. A guard, so it does not grow a restore by symmetry. */
    fresh(CV300_PERI_CRG11_ADDR, 0, HISI_V3);
    seed(CV300_MUX30_ADDR, 0);
    seed(CV300_MUX2C_ADDR, 0);
    setup_hal_hisi();
    reset_counters();
    hal_cleanup();
    CHECK(writes == 0);

    /* Every interleaving of what we do and what the consumer does, six deep.
     * The checker inside the fake polices each one; nothing here has to
     * predict the right answer. It cannot catch the one race the fix does not
     * close -- the consumer re-enabling to the identical word between our arm
     * and our undo -- and that is stated in hal_hisi.c rather than tested. */
    static const int DEPTH = 6;
    const uint32_t seeds[] = {0x00, 0x10, 0x11, 0x13, 0x15};
    int explored = 0;
    for (size_t s = 0; s < ARRCNT(seeds); s++) {
        for (long path = 0; path < 4096; path++) {
            fresh(V4, seeds[s], HISI_V4);
            setup_hal_hisi();
            long p = path;
            for (int step = 0; step < DEPTH; step++, p /= 4) {
                switch (p % 4) {
                case 0: hal_enable_sensor_clock(); break;
                case 1: hal_cleanup(); break;
                case 2: poke(V4, peek(V4) | 0x1); break;
                case 3: poke(V4, peek(V4) & ~0x1u); break;
                }
            }
            explored++;
            if (illegal) {
                fprintf(stderr, "  FAIL seed %#x path %ld: %s\n",
                    (unsigned)seeds[s], path, illegal);
                failures++;
                path = 4096; /* one report per seed is enough */
            }
        }
    }
    CHECK(explored == (int)ARRCNT(seeds) * 4096);

    if (failures)
        fprintf(stderr, "hal_hisi_test: %d failure(s)\n", failures);
    else
        printf("hal_hisi_test: ok\n");
    return failures ? 1 : 0;
}
