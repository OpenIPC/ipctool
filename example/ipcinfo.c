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

static void print_usage() {
    printf(
        "Usage: ipcinfo [OPTIONS]\n"
        "Where:\n"
        "  -c, --chip-name           read chip name\n"
        "  -f, --family              read chip family\n"
        "  -v, --vendor              read chip manufacturer\n"
        "  -l, --long-sensor         read sensor model and control line\n"
        "  -s, --short-sensor        read sensor model\n"
        "  -F, --flash-type          read flash type (nor, nand)\n"
        "  -t, --temp                read chip temperature (where supported)\n"
        "  -x, --xm-mac              read MAC address (for XM chips)\n"
        "  -S, --streamer            read streamer name\n"
        "  -V, --version             display version\n"
        "  -h, --help                display this help\n");
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
        return;

    mtd_info_t mtd_info;
    if (ioctl(devfd, MEMGETINFO, &mtd_info) >= 0) {
        puts(flash_type(mtd_info.type));
    }

    close(devfd);
}

static void print_vendor() {
    const char *vendor = getchipvendor();
    size_t len = strlen(vendor);
    char *str = alloca(len + 1);
    str[len] = 0;
    for (int i = 0; i < len; i++) {
        str[i] = tolower(vendor[i]);
    }
    puts((const char *)str);
}

static void print_streamer() {
    char sname[1024];
    pid_t godpid;

    if ((godpid = get_god_pid(NULL, 0)) > 0) {
        snprintf(sname, sizeof(sname), "/proc/%d/cmdline", godpid);
        FILE *fp = fopen(sname, "r");
        if (!fp)
            exit(EXIT_FAILURE);
        if (!fgets(sname, sizeof(sname), fp))
            exit(EXIT_FAILURE);
        puts(sname);

        fclose(fp);
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
        if (sscanf(dev, "mtd%d: %x %x \"%64[^\"]\"", &i, &es, &ee, name)) {
            if (find_xm_mac(i, es))
                return;
        }
    }
    fclose(fp);
    fprintf(stderr, "Nothing found.\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
    const char *short_options = "cfvhlstFSxV";
    const struct option long_options[] = {
        {"chip-name", no_argument, NULL, 'c'},
        {"family", no_argument, NULL, 'f'},
        {"vendor", no_argument, NULL, 'v'},
        {"help", no_argument, NULL, 'h'},
        {"long-sensor", no_argument, NULL, 'l'},
        {"short-sensor", no_argument, NULL, 's'},
        {"flash-type", no_argument, NULL, 'F'},
        {"temp", no_argument, NULL, 't'},
        {"streamer", no_argument, NULL, 'S'},
        {"xm-mac", no_argument, NULL, 'x'},
        {"version", no_argument, NULL, 'V'},
        {NULL, 0, NULL, 0}};

    int opt;
    int long_index = 0;
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
        }
    }

    if (argc == 1)
        print_usage();

    exit(EXIT_SUCCESS);
}
