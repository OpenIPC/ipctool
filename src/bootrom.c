/* `ipctool bootrom` -- dump or inspect the SoC's mask-ROM region.
 *
 * Default mode (no flags): print YAML metadata -- base address, size,
 * accessibility (whether the region reads as all-zeros, which is the
 * Goke V4 case where the silicon hides the bootrom from userspace
 * post-boot), the first 16 bytes as a hex preview, and the chip name
 * for context.
 *
 * --dump: write the raw binary region to stdout (intended to be
 * redirected to a file or piped to `md5sum` / `xxd`). No metadata
 * preamble in this mode.
 *
 * Per-family defaults sourced from the HI3516CV500-SDK bootrom RE
 * (sdk/bootrom/bootrom.cpp): BOOTROM at 0x04000000, size up to SRAM
 * at 0x04010000 = 64 KB. Confirmed on V4 (hi3516ev300, gk7205v300)
 * and V4A (hi3516av300) lab boards. For unsupported families the
 * caller must pass --base / --size explicitly.
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "bootrom.h"
#include "chipid.h"
#include "cjson/cJSON.h"
#include "cjson/cYAML.h"
#include "hal/hisi/hal_hisi.h"
#include "tools.h"

struct bootrom_default {
    int chip_id;
    uint32_t base;
    uint32_t size;
};

/* Known per-family bootrom locations. Empty (base=0) entries mean
 * "no first-party default known; require --base/--size". */
static const struct bootrom_default defaults[] = {
    {HISI_V4, 0x04000000, 0x10000},
    {HISI_V4A, 0x04000000, 0x10000},
};

static bool default_for_chip(int chip_id, uint32_t *base, uint32_t *size) {
    for (size_t i = 0; i < ARRCNT(defaults); i++) {
        if (defaults[i].chip_id == chip_id) {
            *base = defaults[i].base;
            *size = defaults[i].size;
            return true;
        }
    }
    return false;
}

static bool buffer_is_all_zero(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (p[i] != 0)
            return false;
    return true;
}

static cJSON *bootrom_metadata(const uint8_t *buf, uint32_t base,
                               uint32_t size) {
    cJSON *j_inner = cJSON_CreateObject();
    ADD_PARAM_FMT("base", "0x%08x", base);
    ADD_PARAM_NUM("size", size);
    ADD_PARAM_NUM("accessible", buffer_is_all_zero(buf, size) ? 0 : 1);

    /* First 16 bytes as a hex preview -- enough to spot an ARM exception
     * vector table (typical opener: 1000 00ea 14f0 9fe5 ...) without
     * spamming the terminal. */
    char hex[16 * 3 + 1];
    size_t prev = size < 16 ? size : 16;
    char *p = hex;
    for (size_t i = 0; i < prev; i++) {
        snprintf(p, 4, "%s%02x", i ? " " : "", buf[i]);
        p += (i ? 3 : 2);
    }
    ADD_PARAM("first_bytes", hex);

    const char *chip = getchipname();
    if (chip)
        ADD_PARAM("chip", chip);
    return j_inner;
}

static void print_bootrom_usage(void) {
    printf(
        "Usage: ipctool bootrom [--base ADDR] [--size N] [--dump] [--json]\n"
        "\n"
        "Inspect or dump the SoC mask-ROM region.\n"
        "\n"
        "Default (no flags): YAML metadata (base, size, accessibility,\n"
        "first 16 bytes hex, chip name). `accessible: 0` means the region\n"
        "reads as all zeros -- the Goke V4 case where the silicon hides\n"
        "the bootrom from userspace post-boot.\n"
        "\n"
        "  --dump        write the raw binary region to stdout (redirect\n"
        "                to a file, pipe to `md5sum`, etc.). No metadata\n"
        "                preamble.\n"
        "  --base ADDR   override per-family default (e.g. 0x04000000)\n"
        "  --size N      override per-family default size in bytes\n"
        "  --json        machine-readable JSON metadata (ignored with --dump)\n"
        "\n"
        "Per-family defaults: V4 / V4A = 0x04000000 / 0x10000 (64 KB).\n"
        "Other families: --base / --size required.\n");
}

int bootrom_cmd(int argc, char **argv) {
    uint32_t base = 0, size = 0;
    bool base_set = false, size_set = false;
    bool want_dump = false;
    bool want_json = false;

    const struct option long_options[] = {
        {"base", required_argument, NULL, 'b'},
        {"size", required_argument, NULL, 's'},
        {"dump", no_argument, NULL, 'd'},
        {"json", no_argument, NULL, 'j'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };
    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "b:s:djh", long_options, NULL)) !=
           -1) {
        switch (opt) {
        case 'b':
            base = (uint32_t)strtoul(optarg, NULL, 0);
            base_set = true;
            break;
        case 's':
            size = (uint32_t)strtoul(optarg, NULL, 0);
            if (size == 0 || size > 0x01000000) {
                fprintf(stderr, "bootrom: --size must be 1..16 MB\n");
                return EXIT_FAILURE;
            }
            size_set = true;
            break;
        case 'd':
            want_dump = true;
            break;
        case 'j':
            want_json = true;
            break;
        case 'h':
            print_bootrom_usage();
            return EXIT_SUCCESS;
        default:
            print_bootrom_usage();
            return EXIT_FAILURE;
        }
    }

    /* Fill in family defaults if the user didn't override. */
    if (!base_set || !size_set) {
        if (!getchipname()) {
            fprintf(stderr,
                    "bootrom: cannot identify chip; pass --base and --size\n");
            return EXIT_FAILURE;
        }
        uint32_t def_base, def_size;
        if (!default_for_chip(chip_generation, &def_base, &def_size)) {
            fprintf(stderr,
                    "bootrom: no first-party default for chip family 0x%x "
                    "(%s); pass --base and --size explicitly\n",
                    chip_generation, chip_name);
            return EXIT_FAILURE;
        }
        if (!base_set)
            base = def_base;
        if (!size_set)
            size = def_size;
    }

    int fd = open("/dev/mem", O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "bootrom: open /dev/mem: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    void *map = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, base);
    if (map == MAP_FAILED) {
        fprintf(stderr, "bootrom: mmap 0x%x size %u: %s\n", base, size,
                strerror(errno));
        close(fd);
        return EXIT_FAILURE;
    }

    int rc = EXIT_SUCCESS;
    if (want_dump) {
        size_t left = size;
        const uint8_t *p = map;
        while (left) {
            ssize_t n = write(1, p, left);
            if (n <= 0) {
                fprintf(stderr, "bootrom: write: %s\n", strerror(errno));
                rc = EXIT_FAILURE;
                break;
            }
            p += n;
            left -= n;
        }
    } else {
        cJSON *meta = bootrom_metadata(map, base, size);
        cJSON *root = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "bootrom", meta);
        char *out = want_json ? cJSON_Print(root) : cYAML_Print(root);
        if (out) {
            printf("%s", out);
            if (want_json)
                printf("\n");
            free(out);
        }
        cJSON_Delete(root);
    }

    munmap(map, size);
    close(fd);
    return rc;
}
