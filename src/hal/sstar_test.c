/* The SigmaStar generation register as sstar_detect_cpu() names it.
 *
 * Each generation's name is pinned here: dropping or renaming a case puts that
 * generation back to "unknown" in every report, which is what Infinity2M
 * (0xF0) did until it had one (an SSR621Q in a Jooan JA-8108 NVR, BROM tag
 * ...I2m_ROM...). Infinity2M's parts are named by their bond strap.
 *
 * CHECK rather than assert(), because the release flags carry -DNDEBUG and
 * would compile an assert-based test into one that passes unconditionally.
 *
 * sstar.c is included rather than linked, for the same reason novatek_test.c
 * includes novatek.c: the register read is mem_reg(), answered here. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hal/sstar.c"

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static uint32_t generation_reg; /* what SSTAR_ADDR reads */
static uint32_t bond_reg;       /* what SSTAR_BOND_ADDR reads */
static bool bond_fails;         /* the strap alone unreadable */
static bool mmio_fails;         /* /dev/mem withheld */

bool mem_reg(uint32_t addr, uint32_t *data, enum REG_OPS op) {
    if (mmio_fails || op != OP_READ)
        return false;
    if (addr == SSTAR_ADDR) {
        *data = generation_reg;
        return true;
    }
    if (addr == SSTAR_BOND_ADDR && !bond_fails) {
        *data = bond_reg;
        return true;
    }
    return false;
}

/* The rest of sstar.c's externals; nothing below reads a file. */
bool line_from_file(const char *filename, const char *re, char *buf, size_t n) {
    (void)filename, (void)re, (void)buf, (void)n;
    return false;
}
void disable_printk(void) {}
void restore_printk(void) {}
int chip_generation;
char chip_name[128];

static bool detects(uint32_t reg, const char *want) {
    generation_reg = reg;
    chip_generation = 0;
    strcpy(chip_name, "unknown");
    char name[128] = "unknown";
    bool ok = sstar_detect_cpu(name);
    if (!ok || strcmp(name, want) || chip_generation != (int)reg) {
        fprintf(stderr, "  0x%02X: %s (%d), want %s\n", reg, name, ok, want);
        return false;
    }
    return true;
}

/* An Infinity2M part by its bond strap. */
static bool i2m_detects(uint32_t bond, const char *want) {
    bond_reg = bond;
    bool ok = detects(INFINITY2M, want);
    if (!ok)
        fprintf(stderr, "  bond 0x%X\n", bond);
    bond_reg = 0;
    return ok;
}

int main(void) {
    /* The packages the IPLs name a DDR setting for, by the low byte the
     * IPL reads; the rest of the register is ignored, as there. */
    CHECK(i2m_detects(0x1B, "SSR621Q"));
    CHECK(i2m_detects(0x1D, "SSD201"));
    CHECK(i2m_detects(0x1E, "SSD202D"));
    CHECK(i2m_detects(0x11B, "SSR621Q"));
    /* 0x1F has a DDR setting but no known part; anything else is not a
     * package: the group's name. */
    CHECK(i2m_detects(0x1F, "SSD20X"));
    CHECK(i2m_detects(0x00, "SSD20X"));
    CHECK(i2m_detects(0x3B, "SSD20X"));
    /* The strap unreadable: still the generation, by the group's name. */
    bond_fails = true;
    CHECK(i2m_detects(0x1B, "SSD20X"));
    bond_fails = false;

    CHECK(detects(INFINITY5, "SSC32X"));
    CHECK(detects(INFINITY6, "SSC32X"));
    CHECK(detects(INFINITY6B, "SSC33X"));
    CHECK(detects(INFINITY6E, "SSC33X"));
    CHECK(detects(MERCURY6, "SSC35X"));
    CHECK(detects(INFINITY6C, "SSC37X"));
    CHECK(detects(INFINITY6F, "SSC37X"));

    /* A generation with no case is still SigmaStar's, just unnamed. */
    CHECK(detects(PIONEER3, "unknown"));

    /* No /dev/mem: not detected at all. */
    mmio_fails = true;
    char name[128] = "unknown";
    CHECK(!sstar_detect_cpu(name));
    mmio_fails = false;

    if (failures)
        fprintf(stderr, "sstar_test: %d failure(s)\n", failures);
    else
        printf("sstar_test: ok\n");
    return failures ? 1 : 0;
}
