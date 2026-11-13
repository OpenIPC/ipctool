/*
 * uboot_test: what `ipctool printenv [-n] [name...]` prints and returns.
 * The flash is a stub that hands back one synthetic U-Boot environment, so
 * the command runs end to end -- environment detection included -- on a PC.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mtd.h"
#include "uboot.h"

#define ENV_SIZE 0x10000

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

static uint8_t flash[ENV_SIZE];

static uint32_t crc32_ref(const uint8_t *p, size_t n) {
    uint32_t crc = 0xFFFFFFFF;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return ~crc;
}

/* The environment as U-Boot stores it: CRC, then NUL-separated "k=v" strings
 * ending in an empty one, padded with zeroes. */
static void make_env(const char *const *vars) {
    memset(flash, 0, sizeof(flash));
    uint8_t *p = flash + 4;
    for (; *vars; vars++) {
        strcpy((char *)p, *vars);
        p += strlen(*vars) + 1;
    }
    uint32_t crc = crc32_ref(flash + 4, ENV_SIZE - 4);
    memcpy(flash, &crc, 4);
}

/* --- the parts of mtd.c the command reaches --- */
static bool stub_has_env = true;

char *open_mtdblock(int i, int *fd, uint32_t size, int flags) {
    (void)i;
    (void)size;
    (void)flags;
    *fd = -1;
    return (char *)flash;
}

void enum_mtd_info(void *ctx, cb_mtd cb) {
    if (!stub_has_env)
        return;
    struct mtd_info_user mtd = {.size = ENV_SIZE, .erasesize = ENV_SIZE};
    cb(0, "env", &mtd, ctx);
}

bool mtd_write(int mtd, uint32_t offset, uint32_t erasesize, const char *data,
               size_t size) {
    (void)mtd;
    (void)offset;
    (void)erasesize;
    (void)data;
    (void)size;
    return true;
}

/* Evaluate expr with stdout and stderr captured into out and err. */
#define CAPTURE(rc, out, outsz, err, errsz, expr)                                            \
    do {                                                                       \
        fflush(stdout);                                                        \
        fflush(stderr);                                                        \
        int so = dup(1), se = dup(2);                                          \
        FILE *fo = tmpfile(), *fe = tmpfile();                                 \
        dup2(fileno(fo), 1);                                                   \
        dup2(fileno(fe), 2);                                                   \
        (rc) = (expr);                                                         \
        fflush(stdout);                                                        \
        fflush(stderr);                                                        \
        dup2(so, 1);                                                           \
        dup2(se, 2);                                                           \
        close(so);                                                             \
        close(se);                                                             \
        rewind(fo);                                                            \
        rewind(fe);                                                            \
        size_t no = fread(out, 1, (outsz) - 1, fo);                        \
        size_t ne = fread(err, 1, (errsz) - 1, fe);                        \
        (out)[no] = 0;                                                         \
        (err)[ne] = 0;                                                         \
        fclose(fo);                                                            \
        fclose(fe);                                                            \
    } while (0)

static int run(char *out, size_t outsz, char *err, size_t errsz, int argc,
               char **argv) {
    int rc;
    CAPTURE(rc, out, outsz, err, errsz, cmd_printenv(argc, argv));
    return rc;
}

int main(void) {
    const char *const vars[] = {"bootcmd=run nor", "bootcmdnor=sf probe",
                                "baudrate=115200", "empty=", NULL};
    make_env(vars);

    char out[4096], err[4096];
    int rc;

    /* No names: the whole environment, as it always was. */
    {
        char *argv[] = {"printenv"};
        rc = run(out, sizeof(out), err, sizeof(err), 1, argv);
        CHECK(rc == 0, "rc %d", rc);
        CHECK(!strcmp(out, "bootcmd=run nor\nbootcmdnor=sf probe\n"
                           "baudrate=115200\nempty=\n"),
              "out '%s'", out);
        CHECK(!*err, "err '%s'", err);
    }

    /* One name: that variable, whole-name matched. */
    {
        char *argv[] = {"printenv", "bootcmd"};
        rc = run(out, sizeof(out), err, sizeof(err), 2, argv);
        CHECK(rc == 0, "rc %d", rc);
        CHECK(!strcmp(out, "bootcmd=run nor\n"), "out '%s'", out);
    }

    /* Several names come out in the order asked, not the order stored. */
    {
        char *argv[] = {"printenv", "baudrate", "bootcmdnor"};
        rc = run(out, sizeof(out), err, sizeof(err), 3, argv);
        CHECK(rc == 0, "rc %d", rc);
        CHECK(!strcmp(out, "baudrate=115200\nbootcmdnor=sf probe\n"),
              "out '%s'", out);
    }

    /* A missing name is an error but does not hide the others. */
    {
        char *argv[] = {"printenv", "nosuch", "baudrate"};
        rc = run(out, sizeof(out), err, sizeof(err), 3, argv);
        CHECK(rc == 1, "rc %d", rc);
        CHECK(!strcmp(out, "baudrate=115200\n"), "out '%s'", out);
        CHECK(!strcmp(err, "## Error: \"nosuch\" not defined\n"), "err '%s'",
              err);
    }

    /* A prefix of a name is not the name. */
    {
        char *argv[] = {"printenv", "boot"};
        rc = run(out, sizeof(out), err, sizeof(err), 2, argv);
        CHECK(rc == 1, "rc %d", rc);
        CHECK(!*out, "out '%s'", out);
    }

    /* -n: the bare value, nothing else on stdout. */
    {
        char *argv[] = {"printenv", "-n", "baudrate"};
        rc = run(out, sizeof(out), err, sizeof(err), 3, argv);
        CHECK(rc == 0, "rc %d", rc);
        CHECK(!strcmp(out, "115200\n"), "out '%s'", out);
    }

    /* -n on a variable that is set but empty prints an empty line. */
    {
        char *argv[] = {"printenv", "-n", "empty"};
        rc = run(out, sizeof(out), err, sizeof(err), 3, argv);
        CHECK(rc == 0, "rc %d", rc);
        CHECK(!strcmp(out, "\n"), "out '%s'", out);
    }

    /* -n takes exactly one name. */
    {
        char *argv[] = {"printenv", "-n"};
        rc = run(out, sizeof(out), err, sizeof(err), 2, argv);
        CHECK(rc == 1, "rc %d", rc);
        CHECK(!*out && *err, "out '%s' err '%s'", out, err);

        char *argv2[] = {"printenv", "-n", "baudrate", "empty"};
        rc = run(out, sizeof(out), err, sizeof(err), 4, argv2);
        CHECK(rc == 1, "rc %d", rc);
        CHECK(!*out && *err, "out '%s' err '%s'", out, err);
    }

    /* Asked for a name and no environment exists: a failure, said so. */
    {
        stub_has_env = false;
        char *argv[] = {"printenv", "baudrate"};
        rc = run(out, sizeof(out), err, sizeof(err), 2, argv);
        CHECK(rc == 1, "rc %d", rc);
        CHECK(!*out && *err, "out '%s' err '%s'", out, err);
        stub_has_env = true;
    }

    if (!failed)
        printf("uboot_test: ok\n");
    return failed;
}
