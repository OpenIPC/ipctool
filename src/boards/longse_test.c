/* Hardware-free cover for the Longse Ver.ini parsing. Nobody on the project has
 * one of these cameras, so this is the only check the code gets.
 *
 * CHECK rather than assert(), matching reginfo_test.c: the release flags carry
 * -DNDEBUG, which compiles assert() out entirely and would leave this file
 * passing no matter what longse_version_of() returned. */
#include <stdio.h>
#include <string.h>

#include "boards/longse.h"

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static void expect(const char *sofvar, const char *want) {
    const char *got = longse_version_of(sofvar);

    if (!want) {
        CHECK(got == NULL);
        if (got)
            fprintf(stderr, "    (wanted no version, got \"%s\")\n", got);
        return;
    }
    CHECK(got != NULL && !strcmp(got, want));
    if (!got || strcmp(got, want))
        fprintf(stderr, "    (wanted \"%s\", got \"%s\")\n", want,
                got ? got : "(null)");
}

int main(void) {
    /* Both strings reported in issue #43. */
    expect("RV1109_IMX335NAND_BVH0L0A0T0Q0_W_21.1.36.6", "21.1.36.6");
    expect("3516CV500_IMX307_B1T1A1M0C0P1_W_9.1.53.1", "9.1.53.1");

    /* The first marker wins, and everything after it is the version. */
    expect("3516CV500_IMX307_W_1.0_W_2.0", "1.0_W_2.0");

    /* Nothing to report rather than an empty string or a stray pointer. */
    expect("3516CV500_IMX307_B1T1A1M0C0P1", NULL);
    expect("3516CV500_IMX307_W_", NULL);
    expect("", NULL);
    expect(NULL, NULL);

    /* The marker is anchored on both underscores: a bare W does not start it. */
    expect("3516CV500_W1.0", NULL);

    if (failures) {
        fprintf(stderr, "longse_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("longse_test: all cases passed\n");
    return 0;
}
