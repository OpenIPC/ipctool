/* Hardware-free cover for the Axera part naming: the Hamming(7,4) decode of
 * the AX620E-family eFuse chip type, checked against BL1's own
 * hanming_decode_74() (boot/bl1/board/board.c, transcribed below), and the
 * AX620U-family DDR-width and bond-option rule from ax_hwinfo.ko.
 *
 * CHECK rather than assert(), as in anyka_test.c: the release flags carry
 * -DNDEBUG, which compiles assert() out. */
#include <stdio.h>
#include <string.h>

/* axera.c is included for its file-statics, as novatek_test.c includes
 * novatek.c: static, the decode folds into axera_detect_cpu() and costs
 * ipcinfo no function frames of its own. */
#include "hal/axera.c"

int chip_generation;
char chip_name[128];

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                        \
        }                                                                      \
    } while (0)

/* The SDK's decoder, verbatim but for layout: the reference the HAL is held
 * to on every one of the 128 seven-bit codes. */
static unsigned char hanming_decode_74(unsigned char a) {
    unsigned char b = a, c = 0;

    if (b & 0x40)
        c ^= 0x07;
    if (b & 0x20)
        c ^= 0x06;
    if (b & 0x10)
        c ^= 0x05;
    if (b & 0x08)
        c ^= 0x03;
    if (b & 0x04)
        c ^= 0x04;
    if (b & 0x02)
        c ^= 0x02;
    if (b & 0x01)
        c ^= 0x01;
    switch (c) {
    case 3:
        b = (b ^ 0x08) >> 3;
        break;
    case 5:
        b = (b ^ 0x10) >> 3;
        break;
    case 6:
        b = (b ^ 0x20) >> 3;
        break;
    case 7:
        b = (b ^ 0x40) >> 3;
        break;
    default:
        b = b >> 3;
        break;
    }
    return b & 0x0F;
}

/* What the fuse programmer burns: the type in bits 6:3, parity in 2:0. */
static unsigned char encode(unsigned type) {
    static const unsigned char column[4] = {3, 5, 6, 7};
    unsigned char parity = 0;

    for (int i = 0; i < 4; i++)
        if (type & (1u << i))
            parity ^= column[i];
    return (unsigned char)(type << 3 | parity);
}

static void expect_e(uint32_t efuse, uint32_t sw9, const char *want) {
    const char *got = axera_ax620e_name(efuse, sw9);

    if (!want ? got != NULL : !got || strcmp(got, want)) {
        fprintf(stderr, "  FAIL efuse 0x%08x sw9 %u: got %s, want %s\n", efuse,
                sw9, got ? got : "(null)", want ? want : "(null)");
        failures++;
    }
}

int main() {
    static const char *const names[] = {
        NULL,     "ax620q",  "ax620qx", "ax620v200", "ax630c",
        "ax631",  "ax620qz", "ax620qp", "ax620qe",   "ax620qf",
    };

    /* Every code, clean or corrupted, decodes as BL1 decodes it. */
    for (unsigned code = 0; code < 0x100; code++)
        CHECK(axera_hamming74_decode(code) == hanming_decode_74(code));

    /* Every listed type, from a clean code and from each single-bit flip
     * of it, with the rest of block 2 (thm_vref, bgs, trim) set. */
    for (unsigned type = 1; type < 10; type++) {
        unsigned char code = encode(type);

        CHECK(hanming_decode_74(code) == type);
        expect_e((uint32_t)code << 24 | 0x00ffffff, 0, names[type]);
        for (int bit = 0; bit < 7; bit++)
            expect_e((uint32_t)(code ^ (1u << bit)) << 24, 0, names[type]);
    }

    /* The eFuse wins over DUMMY_SW9. */
    expect_e((uint32_t)encode(1) << 24, 9, "ax620q");
    /* A word reading 0 is either unfused or closed to the normal world:
     * DUMMY_SW9, where BL1 left the type, answers, and with nothing there
     * either it is BL1's default, AX630C. */
    expect_e(0, 9, "ax620qf");
    expect_e(0, 0, "ax630c");
    expect_e(0, 0xdead, "ax630c");
    /* A type the SDK does not list: the family name. */
    expect_e((uint32_t)encode(12) << 24, 0, "ax620e");

    CHECK(!strcmp(axera_ax620u_name(0, 0), "ax620u"));
    CHECK(!strcmp(axera_ax620u_name(0xfeffffff, 0xffffffff), "ax620u"));
    CHECK(!strcmp(axera_ax620u_name(1u << 24, 0), "ax620a"));
    CHECK(!strcmp(axera_ax620u_name(1u << 24, 0x00181800), "ax620a"));
    CHECK(!strcmp(axera_ax620u_name(1u << 24, 0x00191800), "ax620d"));
    CHECK(!strcmp(axera_ax620u_name(1u << 24, 0xffffffff), "ax620d"));

    if (failures) {
        fprintf(stderr, "axera_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("axera_test: all checks passed\n");
    return 0;
}
