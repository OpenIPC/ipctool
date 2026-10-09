/* mtd_alias_test: which partitions are another name for the whole chip. */
#include <stdio.h>

#include "mtd_alias.h"

static int failed;
#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);               \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, "\n");                                             \
            failed = 1;                                                        \
        }                                                                      \
    } while (0)

int main(void) {
    // Novatek NT98566 on 128 MB NAND (ipctool#234): twelve partitions, the
    // last one "all".
    uint64_t off[12] = {0,        0x40000,  0x80000,   0xc0000,
                        0x2c0000, 0x2e0000, 0x300000,  0x800000,
                        0x6000000, 0x7000000, 0x7c00000, 0};
    uint64_t size[12] = {0x40000,   0x40000,  0x40000,  0x200000,
                         0x20000,   0x20000,  0x500000, 0x5800000,
                         0x1000000, 0xc00000, 0x400000, 0x8000000};
    bool has[12], alias[12];
    for (int i = 0; i < 12; i++)
        has[i] = true;
    mtd_mark_aliases(12, off, has, size, alias);
    for (int i = 0; i < 11; i++)
        CHECK(!alias[i], "partition %d called an alias", i);
    CHECK(alias[11], "\"all\" not called an alias");

    // The same without offsets (an old kernel): by size.
    for (int i = 0; i < 12; i++)
        has[i] = false;
    mtd_mark_aliases(12, off, has, size, alias);
    for (int i = 0; i < 11; i++)
        CHECK(!alias[i], "by size: partition %d called an alias", i);
    CHECK(alias[11], "by size: \"all\" not called an alias");

    // An ordinary NOR layout: nothing is an alias, even where one partition
    // is the size of the others combined by chance with offsets known.
    uint64_t noff[4] = {0, 0x40000, 0x50000, 0x250000};
    uint64_t nsize[4] = {0x40000, 0x10000, 0x200000, 0x250000};
    bool nhas[4] = {true, true, true, true}, nalias[4];
    mtd_mark_aliases(4, noff, nhas, nsize, nalias);
    for (int i = 0; i < 4; i++)
        CHECK(!nalias[i], "NOR partition %d called an alias", i);

    // One partition alone is never an alias.
    uint64_t one = 0x1000000, zero = 0;
    bool f = false, a;
    mtd_mark_aliases(1, &zero, &f, &one, &a);
    CHECK(!a, "a lone partition called an alias");

    if (!failed)
        printf("mtd_alias_test: ok\n");
    return failed;
}
