/* upgrade_env_test: the U-Boot variables `ipctool upgrade` edits. */
#include <stdio.h>
#include <string.h>

#include "upgrade_env.h"

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

// A XiongMai 85H50AI (Hi3516EV300, 16 MB NOR) on OpenIPC lite: the vendor
// bootloader keeps the whole line literal, the partition after boot is "wtf"
// rather than "env", and the line carries an mmz carve-out the old upgrade
// path regenerated away.
static const char *XM_BOOTARGS =
    "mem=128M console=ttyAMA0,115200 panic=20 rootfstype=squashfs "
    "root=/dev/mtdblock3 init=/init "
    "mtdparts=hi_sfc:256k(boot),64k(wtf),2048k(kernel),5120k(rootfs),-(rootfs_"
    "data) mmz_allocator=cma mmz=anonymous,0,0x42000000,96M";
static const char *XM_BOOTCMD =
    "setenv setargs setenv bootargs ${bootargs}; run setargs; sf probe 0; sf "
    "read 0x42000000 0x50000 0x200000; bootm 0x42000000";

int main(void) {
    char id[64], out[1024];

    CHECK(upgrade_mtd_id(XM_BOOTARGS, id, sizeof(id)), "no mtd id");
    CHECK(!strcmp(id, "hi_sfc"), "mtd id '%s'", id);
    CHECK(!upgrade_mtd_id("mtdparts=${mtdparts} root=/dev/mtdblock3", id,
                          sizeof(id)),
          "a variable taken for a literal mtdparts");
    CHECK(!upgrade_mtd_id("console=ttyAMA0 root=/dev/mtdblock3", id,
                          sizeof(id)),
          "mtd id found where there is no mtdparts");
    CHECK(!upgrade_mtd_id("xmtdparts=hi_sfc:1k(a)", id, sizeof(id)),
          "a word ending in mtdparts= taken for it");

    // To the ultimate layout: kernel 3 MB, rootfs 10 MB, everything else as
    // it was, in place.
    CHECK(upgrade_bootargs(XM_BOOTARGS, 3,
                           "hi_sfc:256k(boot),64k(wtf),3072k(kernel),10240k("
                           "rootfs),-(rootfs_data)",
                           out, sizeof(out)),
          "bootargs refused");
    CHECK(!strcmp(out,
                  "mem=128M console=ttyAMA0,115200 panic=20 rootfstype=squashfs "
                  "root=/dev/mtdblock3 init=/init "
                  "mtdparts=hi_sfc:256k(boot),64k(wtf),3072k(kernel),10240k("
                  "rootfs),-(rootfs_data) mmz_allocator=cma "
                  "mmz=anonymous,0,0x42000000,96M"),
          "bootargs '%s'", out);

    CHECK(upgrade_bootargs("root=/dev/mtdblock3 mtdparts=sfc:1k(a)", 4,
                           "sfc:2k(a)", out, sizeof(out)) &&
              !strcmp(out, "root=/dev/mtdblock4 mtdparts=sfc:2k(a)"),
          "root index not moved: '%s'", out);
    CHECK(!upgrade_bootargs("console=ttyAMA0 mtdparts=sfc:1k(a)", 3,
                            "sfc:2k(a)", out, sizeof(out)),
          "a line without root= edited");
    CHECK(upgrade_bootargs("root=/dev/mtdblock3 mtdparts=${mtdparts}", 4,
                           "sfc:2k(a)", out, sizeof(out)) &&
              !strcmp(out, "root=/dev/mtdblock4 mtdparts=${mtdparts}"),
          "a variable mtdparts was not left for its variable: '%s'", out);
    CHECK(!upgrade_bootargs(XM_BOOTARGS, 3, "hi_sfc:1k(a)", out, 32),
          "a line that does not fit returned as done");
    CHECK(!upgrade_bootargs(NULL, 3, "hi_sfc:1k(a)", out, sizeof(out)),
          "no bootargs edited");

    CHECK(upgrade_bootcmd(XM_BOOTCMD, 0x50000, 0x300000, out, sizeof(out)),
          "bootcmd refused");
    CHECK(!strcmp(out, "setenv setargs setenv bootargs ${bootargs}; run "
                       "setargs; sf probe 0; sf read 0x42000000 0x50000 "
                       "0x300000; bootm 0x42000000"),
          "bootcmd '%s'", out);
    CHECK(!upgrade_bootcmd("bootm 0x42000000", 0x50000, 0x300000, out,
                           sizeof(out)),
          "a bootcmd without a kernel read edited");
    CHECK(!upgrade_bootcmd("sf read 0x42000000 0x50000 0x200000; sf read "
                           "0x43000000 0x250000 0x100000",
                           0x50000, 0x300000, out, sizeof(out)),
          "two reads: which one is the kernel was guessed");
    CHECK(!upgrade_bootcmd("sf read 0x42000000 0x50000", 0x50000, 0x300000,
                           out, sizeof(out)),
          "a read without a size edited");

    // OpenIPC's own bootloader, as a camera flashed with it keeps its
    // environment: the load address is a variable, and the partitions are
    // also kept in a variable of their own.
    const char *OI_BOOTCMD =
        "setenv setargs setenv bootargs ${bootargs}; run setargs; sf probe 0; "
        "sf read ${baseaddr} 0x50000 0x200000; bootm ${baseaddr}";
    CHECK(upgrade_bootcmd(OI_BOOTCMD, 0x50000, 0x300000, out, sizeof(out)) &&
              !strcmp(out, "setenv setargs setenv bootargs ${bootargs}; run "
                           "setargs; sf probe 0; sf read ${baseaddr} 0x50000 "
                           "0x300000; bootm ${baseaddr}"),
          "variable load address: '%s'", out);
    CHECK(!upgrade_bootcmd("sf read ${baseaddr}; bootm", 0x50000, 0x300000,
                           out, sizeof(out)),
          "a read with no offset edited");

    CHECK(upgrade_mtd_id_value("hi_sfc:256k(boot),64k(env)", id, sizeof(id)) &&
              !strcmp(id, "hi_sfc"),
          "mtd id from the variable '%s'", id);
    // Its default bootargs name the variable: root= moves, mtdparts= stays
    // and the variable is what changes
    const char *OI_DEFAULT = "mem=${osmem} console=ttyAMA0,115200 panic=20 "
                             "root=/dev/mtdblock3 rootfstype=squashfs "
                             "init=/init mtdparts=${mtdparts} ${extras}";
    CHECK(upgrade_bootargs_mtdparts_is_var(OI_DEFAULT), "variable not seen");
    CHECK(!upgrade_bootargs_mtdparts_is_var(XM_BOOTARGS),
          "a literal taken for a variable");
    CHECK(upgrade_bootargs(OI_DEFAULT, 3, "sfc:9k(x)", out, sizeof(out)) &&
              !strcmp(out, OI_DEFAULT),
          "a variable mtdparts= was replaced: '%s'", out);

    if (!failed)
        puts("upgrade_env_test: OK");
    return failed;
}
