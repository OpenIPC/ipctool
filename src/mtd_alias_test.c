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

    // A parent partition with a split inside it (hi3516cv610, 8 MB NOR):
    // firmware is kernel + rootfs, while rootfs_data is only rootfs's tail,
    // and dropping rootfs for holding it lost the squashfs from backups.
    const char *cv[6] = {"u-boot", "env",    "firmware",
                         "kernel", "rootfs", "rootfs_data"};
    uint64_t coff[6] = {0, 0x40000, 0x50000, 0x50000, 0x250000, 0x6d0000};
    uint64_t csize[6] = {0x40000,  0x10000,  0x7b0000,
                         0x200000, 0x5b0000, 0x130000};
    bool chas[6] = {true, true, true, true, true, true}, calias[6];
    mtd_mark_aliases(6, coff, chas, csize, calias);
    for (int i = 0; i < 6; i++)
        CHECK(calias[i] == (i == 2), "%s alias=%d", cv[i], calias[i]);

    // A parent of parents goes too; a parent with a hole in it stays
    uint64_t goff[5] = {0, 0x100000, 0, 0x100000, 0x300000};
    uint64_t gsize[5] = {0x800000, 0x700000, 0x100000, 0x200000, 0x500000};
    bool ghas[5] = {true, true, true, true, true}, galias[5];
    mtd_mark_aliases(5, goff, ghas, gsize, galias);
    CHECK(galias[0] && galias[1] && !galias[2] && !galias[3] && !galias[4],
          "parent of parents");
    uint64_t hoff[3] = {0, 0, 0x200000};
    uint64_t hsize[3] = {0x400000, 0x100000, 0x200000};
    bool hhas[3] = {true, true, true}, halias[3];
    mtd_mark_aliases(3, hoff, hhas, hsize, halias);
    CHECK(!halias[0], "a parent with a hole called an alias");

    // Listed in any order
    uint64_t roff[3] = {0x200000, 0, 0};
    uint64_t rsize[3] = {0x200000, 0x400000, 0x200000};
    bool rhas[3] = {true, true, true}, ralias[3];
    mtd_mark_aliases(3, roff, rhas, rsize, ralias);
    CHECK(!ralias[0] && ralias[1] && !ralias[2], "out of order");

    // One partition alone is never an alias.
    uint64_t one = 0x1000000, zero = 0;
    bool f = false, a;
    mtd_mark_aliases(1, &zero, &f, &one, &a);
    CHECK(!a, "a lone partition called an alias");

    if (!failed)
        printf("mtd_alias_test: ok\n");
    // Where a partition lies, as a kernel without sysfs offsets tells it
    uint64_t at;
    const char *hi = "mem=32M console=ttyAMA0,115200 root=/dev/mtdblock3 "
                     "mtdparts=hi_sfc:256k(boot),64k(env),2048k(kernel),"
                     "5120k(rootfs),-(rootfs_data) panic=20";
    CHECK(mtd_offset_from_cmdline(hi, "boot", 0x40000, &at) && at == 0, "boot");
    CHECK(mtd_offset_from_cmdline(hi, "rootfs", 0x500000, &at) &&
              at == 0x250000,
          "rootfs at %llx", (unsigned long long)at);
    // "-" says nothing of the size, and the size is not asked of it
    CHECK(mtd_offset_from_cmdline(hi, "rootfs_data", 0x2b0000, &at) &&
              at == 0x750000,
          "rootfs_data at %llx", (unsigned long long)at);
    // The size is what says this table is the one the kernel used
    CHECK(!mtd_offset_from_cmdline(hi, "boot", 0x20000, &at), "wrong size");
    CHECK(!mtd_offset_from_cmdline(hi, "nope", 0x40000, &at), "wrong name");
    CHECK(!mtd_offset_from_cmdline("root=/dev/ram", "boot", 0x40000, &at),
          "no mtdparts");

    const char *over = "mtdparts=spi0.0:1M@0x10000(a)ro,0x20000(b),"
                       "16M@0(all),8m@2M(c)lk;nand:1g(d)";
    CHECK(mtd_offset_from_cmdline(over, "a", 0x100000, &at) && at == 0x10000,
          "@ and ro");
    CHECK(mtd_offset_from_cmdline(over, "b", 0x20000, &at) && at == 0x110000,
          "after @");
    CHECK(mtd_offset_from_cmdline(over, "all", 0x1000000, &at) && at == 0,
          "all");
    CHECK(mtd_offset_from_cmdline(over, "c", 0x800000, &at) && at == 0x200000,
          "suffixed @ and lk");
    CHECK(mtd_offset_from_cmdline(over, "d", 0x40000000, &at) && at == 0,
          "second chip");

    // The last mtdparts= is the one the kernel keeps, not a lookalike
    CHECK(mtd_offset_from_cmdline("mtdparts=a:1M(x) mtdparts=a:1M@2M(x)", "x",
                                  0x100000, &at) &&
              at == 0x200000,
          "last wins");
    CHECK(!mtd_offset_from_cmdline("xmtdparts=a:1M(x)", "x", 0x100000, &at),
          "lookalike");
    // Two chips, one name and size: which is it
    CHECK(!mtd_offset_from_cmdline("mtdparts=a:1M(x);b:1M@1M(x)", "x", 0x100000,
                                   &at),
          "ambiguous");
    CHECK(!mtd_offset_from_cmdline("mtdparts=a:zz(x)", "x", 0x100000, &at),
          "garbage");
    CHECK(!mtd_offset_from_cmdline("mtdparts=a:1M(x", "x", 0x100000, &at),
          "unterminated");

    // The kernel's own format, on 3.0 to 6.x, in the shapes dmesg gives it
    static const char klog[] =
        "<5>Creating 5 MTD partitions on \"hi_sfc\":\n"
        "<5>0x000000000000-0x000000020000 : \"BOOT\"\n"
        "<5>[    1.234567] 0x000000040000-0x0000001d0000 : \"KERNEL\"\n"
        "0x000000020000-0x000000040000 : \"UBOOT\"\n"
        "<5>0x000000100000-0x000000110000 : \"twice\"\n"
        "<5>0x000000200000-0x000000210000 : \"twice\"\n"
        "<5>0x000000300000-0x000000310000 : \"same\"\n"
        "<5>0x000000300000-0x000000310000 : \"same\"\n"
        "<6>spi: at 0x12340000 irq 5\n"
        "<5>0x0000007b0000-0x000000800000 : \"cut";
    size_t klen = sizeof(klog) - 1;
    CHECK(mtd_offset_from_log(klog, klen, "BOOT", 0x20000, &at) && at == 0,
          "BOOT");
    CHECK(mtd_offset_from_log(klog, klen, "KERNEL", 0x190000, &at) &&
              at == 0x40000,
          "timestamped");
    CHECK(mtd_offset_from_log(klog, klen, "UBOOT", 0x20000, &at) &&
              at == 0x20000,
          "bare");
    CHECK(!mtd_offset_from_log(klog, klen, "BOOT", 0x10000, &at), "wrong size");
    CHECK(!mtd_offset_from_log(klog, klen, "nope", 0x20000, &at), "wrong name");
    CHECK(!mtd_offset_from_log(klog, klen, "twice", 0x10000, &at),
          "two places");
    CHECK(mtd_offset_from_log(klog, klen, "same", 0x10000, &at) &&
              at == 0x300000,
          "logged twice");
    CHECK(!mtd_offset_from_log(klog, klen, "cut", 0x50000, &at), "torn line");
    // Bounded by len: nothing after it is read
    CHECK(!mtd_offset_from_log(klog, 10, "BOOT", 0x20000, &at), "len");
    CHECK(!mtd_offset_from_log("", 0, "BOOT", 0x20000, &at), "empty");

    return failed;
}
