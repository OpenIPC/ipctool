#include <ctype.h>
#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <mtd/mtd-user.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <ipchw.h>
#include <hal/common.h>

#include "chipid.h"
#include "tools.h"
#include "version.h"

static bool find_xm_mac(int i, size_t size) {
    char filepath[80];

    snprintf(filepath, sizeof(filepath), "/dev/mtdblock%d", i);
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "\"%s \" could not open\n", filepath);
        return false;
    }

    const char *part = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (part == MAP_FAILED) {
        fprintf(stderr, "Mapping Failed\n");
        return false;
    }

    for (size_t off = 0xfc00; off < size; off += 0x10000) {
        const char *ptr = part + off;

        uint16_t header = *(uint16_t *)ptr;
        if (header != 0xd4d2)
            continue;

        uint8_t mac[6] = {ptr[0x379 + 0] - 1, ptr[0x37b] - 3, ptr[0x37d] - 5,
                          ptr[0x37f] - 7,     ptr[0x381] - 9, ptr[0x383] - 11};

        printf("%02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2],
               mac[3], mac[4], mac[5]);
        return true;
    }

    close(fd);

    return false;
}

/* The options, and the only place one is spelled out. The getopt short string,
 * the long-option array and the help text were three separate literals that
 * had to agree by hand, so every long name was written twice and the help
 * carried its own column padding -- 1023 bytes of rodata for the help alone,
 * more than nine of the eleven sensor probes cost, in a binary that ships on
 * all but three of the firmware tree's board configs.
 *
 * Order is the order -h prints. Listing them as string literals rather than
 * char is what lets the blob below be built by concatenation. */
#define IPCINFO_OPTIONS                                                        \
    X("c", "chip-name", "read chip name")                                      \
    X("f", "family", "read chip family")                                       \
    X("v", "vendor", "read chip manufacturer")                                 \
    X("l", "long-sensor", "read sensor model and control line")                \
    X("s", "short-sensor", "read sensor model")                                \
    X("F", "flash-type", "read flash type (nor, nand)")                        \
    X("t", "temp", "read chip temperature (where supported)")                  \
    X("i", "info", "read chip serial (where supported)")                       \
    X("x", "xm-mac", "read MAC address (for XM chips)")                        \
    X("S", "streamer", "read streamer name")                                   \
    X("V", "version", "display version")                                       \
    X("h", "help", "display this help")

#define X(s, n, d) +1
enum { N_OPTIONS = 0 IPCINFO_OPTIONS };
#undef X

#define X(s, n, d) s
static const char short_options[] = IPCINFO_OPTIONS;
#undef X

/* One object, so a PIE build pays no relocation for it. An array of
 * {char, const char *, const char *} costs 8 bytes of .rel.dyn per pointer,
 * which is most of what dropping the duplicated names would have saved --
 * measured, it turned an 840-byte win into 208. Each record is the option
 * letter, then the long name and the description, each NUL-terminated. */
#define X(s, n, d) s n "\0" d "\0"
static const char options_blob[] = IPCINFO_OPTIONS;
#undef X

/* Step over one record, handing back its three fields. */
static const char *option_next(const char *p, char *opt, const char **name,
                               const char **desc) {
    *opt = *p++;
    *name = p;
    p += strlen(p) + 1;
    *desc = p;
    return p + strlen(p) + 1;
}

static void print_usage() {
    const char *p = options_blob;
    char opt;
    const char *name, *desc;

    printf("Usage: ipcinfo [OPTIONS]\nWhere:\n");
    while (*p) {
        p = option_next(p, &opt, &name, &desc);
        /* 19 plus the separating space is the column the hand-padded text
         * used, so -h output is unchanged byte for byte. */
        printf("  -%c, --%-19s %s\n", opt, name, desc);
    }
}

static void print_chip_family() {
    const char *family = getchipfamily();
    if (!family)
        exit(EXIT_FAILURE);
    puts(family);
}

static void print_chip_name() {
    const char *chipname = getchipname();
    if (!chipname)
        exit(EXIT_FAILURE);
    puts(chipname);
}

static void print_chip_temperature() {
    float temp = gethwtemp();
    if (isnan(temp)) {
        fprintf(stderr, "Temperature cannot be retrieved\n");
        exit(EXIT_FAILURE);
    }
    printf("%.2f\n", temp);
}

static void print_serial() {
    char serial[512];
    bool found = false;

    const char *vendor = getchipvendor();
    if (strstr(vendor, VENDOR_HISI) || strstr(vendor, VENDOR_GOKE))
        found = hisi_get_die_id(serial, sizeof serial);
#ifdef IPCHW_VENDOR_SSTAR
    if (strstr(vendor, VENDOR_SSTAR))
        found = sstar_get_die_id(serial, sizeof serial);
#endif

    // Provisioning scripts derive a MAC from this, so a miss has to be silent
    // on stdout and non-zero on exit.
    if (!found)
        exit(EXIT_FAILURE);
    puts(serial);
}

static void print_sensor_long() {
    const char *sensor = getsensoridentity();
    if (!sensor)
        exit(EXIT_FAILURE);
    puts(sensor);
}

static void print_sensor_short() {
    const char *sensor = getsensorshort();
    if (!sensor)
        exit(EXIT_FAILURE);
    puts(sensor);
}

static const char *flash_type(int a) {
    switch (a) {
    case MTD_NORFLASH:
        return "nor";
    case MTD_NANDFLASH:
        return "nand";
    default:
        return "";
    }
}

static void print_flash_type() {
    int devfd = open("/dev/mtd0", O_RDONLY);
    if (devfd < 0)
        exit(EXIT_FAILURE);

    mtd_info_t mtd_info;
    if (ioctl(devfd, MEMGETINFO, &mtd_info) < 0)
        exit(EXIT_FAILURE);

    puts(flash_type(mtd_info.type));
    close(devfd);
}

static void print_vendor() {
    const char *vendor = getchipvendor();
    size_t len = strlen(vendor);
    char *str = alloca(len + 1);
    str[len] = 0;
    for (size_t i = 0; i < len; i++) {
        str[i] = tolower(vendor[i]);
    }
    puts((const char *)str);
}

static void print_streamer() {
    char sname[1024];
    pid_t godpid;

    if ((godpid = get_god_pid(NULL, 0)) > 0) {
        if (get_pid_cmdline(godpid, sname))
            puts(sname);
    }
}

static void print_version() {
#ifndef SKIP_VERSION
    printf("ipcinfo, version: ");
    const char *vers = get_git_version();
    if (*vers) {
        puts(vers);
    } else {
        printf("%s+%s\n", get_git_branch(), get_git_revision());
    }
#endif
}

static void print_xm_mac() {
    FILE *fp;
    char dev[80], name[80];
    int i, es, ee;

    fp = fopen("/proc/mtd", "r");
    if (!fp) {
        fprintf(stderr, "Could not open /proc/mtd.\n");
        exit(EXIT_FAILURE);
    }

    while (fgets(dev, sizeof dev, fp)) {
        name[0] = 0;
        if (sscanf(dev, "mtd%d: %x %x \"%64[^\"]\"", &i, &es, &ee, name) && i<=1) {
            if (find_xm_mac(i, es))
                return;
        }
    }
    fclose(fp);
    fprintf(stderr, "Nothing found.\n");
    exit(EXIT_FAILURE);
}

/* Every reporter here goes through getchipname(), which runs setup_hal_*():
 * printk is silenced and, on HISI_OT, the sensor clock is force-enabled.
 * ipcinfo exits straight out of the reporters, so without this the console
 * stays quiet and the CRG stays modified for the rest of boot -- which
 * ethaddr_provision() in rcS would do on every V5 boot. hal_cleanup is only
 * set once a HAL has been selected, and both restore paths are idempotent.
 */
static void cleanup_hal(void) {
    if (hal_cleanup)
        hal_cleanup();
}

int main(int argc, char **argv) {
    /* Built here rather than as a static initialiser: a static array of
     * struct option is 16 bytes and one relocation per entry in .data.rel.ro,
     * whereas this one lives on the stack and costs nothing in the image. */
    struct option long_options[N_OPTIONS + 1];
    const char *p = options_blob;

    for (int i = 0; i < N_OPTIONS; i++) {
        char opt;
        const char *desc;
        p = option_next(p, &opt, &long_options[i].name, &desc);
        long_options[i].has_arg = no_argument;
        long_options[i].flag = NULL;
        long_options[i].val = opt;
    }
    memset(&long_options[N_OPTIONS], 0, sizeof(long_options[0]));

    int opt;
    int long_index = 0;
    atexit(cleanup_hal);
    while ((opt = getopt_long_only(argc, argv, short_options, long_options,
                                   &long_index)) != -1) {
        switch (opt) {
        case 'c':
            print_chip_name();
            break;
        case 'f':
            print_chip_family();
            break;
        case 'h':
            print_usage();
            break;
        case 'l':
            print_sensor_long();
            break;
        case 's':
            print_sensor_short();
            break;
        case 'F':
            print_flash_type();
            break;
        case 't':
            print_chip_temperature();
            break;
        case 'i':
            print_serial();
            break;
        case 'v':
            print_vendor();
            break;
        case 'S':
            print_streamer();
            break;
        case 'x':
            print_xm_mac();
            break;
        case 'V':
            print_version();
            break;
        default:
            print_usage();
            exit(EXIT_FAILURE);
        }
    }

    if (argc == 1)
        print_usage();

    exit(EXIT_SUCCESS);
}
