/*
 * What the SD card says about itself, and -- on the few lines that implement
 * it -- how much of its rated life it has used.
 *
 * There is no SMART on SD. The CID and CSD say what the card was sold as and
 * nothing about its condition, and the SD Status register adds a speed class
 * and an erase size. The one place wear appears at all is a VENDOR register
 * read with CMD56 (GEN_CMD), which only industrial and surveillance lines
 * implement: SanDisk Industrial, WD Purple, and others with their own
 * arguments and their own layouts.
 *
 * Measured on a WD Purple QD101 in an hi3516av300 (HiSilicon himci, kernel
 * 4.9): the read costs about 5 ms, works while the camera is recording, and
 * does not disturb it.
 *
 * TWO THINGS THIS DELIBERATELY WILL NOT DO.
 *
 * It does not probe a card whose manufacturer id is not in the table below.
 * CMD56 with an argument a card does not implement is not free: measured on
 * that board, the card answers the data phase with a timeout and then raises
 * its ERROR status bit, and the NEXT command on the host fails once before
 * the card clears it. Harmless to a diagnostic run by hand, and not something
 * to hand a camera that is writing video.
 *
 * And it does not decode a vendor it has not been read against. The layouts
 * below are the ones checked on real cards; a plausible guess at another
 * vendor's register would print a life figure nobody measured, which is worse
 * than printing nothing.
 */
#include "sdcard.h"

#include "cjson/cJSON.h"
#include "cjson/cYAML.h"
#include "tools.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* linux/mmc/ioctl.h is not in every toolchain's sysroot, and the ABI is
 * stable, so the request is spelled out here rather than depended on. */
struct mmc_ioc_cmd_compat {
    int write_flag;
    int is_acmd;
    uint32_t opcode;
    uint32_t arg;
    uint32_t response[4];
    unsigned int flags;
    unsigned int blksz;
    unsigned int blocks;
    unsigned int postsleep_min_us;
    unsigned int postsleep_max_us;
    unsigned int data_timeout_ns;
    unsigned int cmd_timeout_ms;
    uint32_t __pad;
    uint64_t data_ptr;
};
#define MMC_IOC_CMD_COMPAT _IOWR(0xB3, 0, struct mmc_ioc_cmd_compat)

#define MMC_RSP_PRESENT (1 << 0)
#define MMC_RSP_CRC (1 << 2)
#define MMC_RSP_OPCODE (1 << 4)
#define MMC_CMD_ADTC (1 << 5)
#define MMC_RSP_SPI_S1 (1 << 7)
#define MMC_RSP_R1 (MMC_RSP_PRESENT | MMC_RSP_CRC | MMC_RSP_OPCODE)

#define SD_GEN_CMD 56
#define HEALTH_LEN 512

/* Vendors whose health register has actually been read, and how.
 *
 * `arg` is the CMD56 argument (bit 0 set means "read"), `sig` the bytes the
 * reply must open with, and `life_used` the offset of the percentage of rated
 * life consumed. A card whose manufacturer id is not here is left alone. */
struct health_layout {
    unsigned manfid;
    const char *vendor;
    uint32_t arg;
    const char *sig[2];
    unsigned life_used;
};

static const struct health_layout layouts[] = {
    /* SanDisk, and Western Digital since it bought them -- both ship MID 0x03,
     * so a WD Purple is indistinguishable from a SanDisk by the CID alone and
     * the register is where the brand actually appears. Read on a WD Purple
     * QD101: signature "DW", "Western Digital" in ASCII at 0x31, the card's
     * own CID embedded at 0x195, life used at byte 8. */
    {0x03, "SanDisk / Western Digital", 0x00000001, {"DS", "DW"}, 8},
};

static const struct health_layout *layout_for(unsigned manfid) {
    for (size_t i = 0; i < sizeof(layouts) / sizeof(layouts[0]); i++)
        if (layouts[i].manfid == manfid)
            return &layouts[i];
    return NULL;
}

/* One line out of /sys, trimmed. */
static bool sysfs_str(const char *dev, const char *attr, char *out,
                      size_t cap) {
    char path[256];
    snprintf(path, sizeof(path), "/sys/block/%s/device/%s", dev, attr);
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    if (!fgets(out, cap, f)) {
        fclose(f);
        return false;
    }
    fclose(f);
    size_t n = strlen(out);
    while (n && (out[n - 1] == '\n' || out[n - 1] == ' '))
        out[--n] = 0;
    return n > 0;
}

/* The whole-device node, which is what the ioctl needs: the kernel refuses
 * MMC_IOC_CMD on a partition with EPERM, to keep one partition's commands out
 * of its siblings. */
static bool find_card(char *dev, size_t cap) {
    for (int i = 0; i < 4; i++) {
        char name[8];
        snprintf(name, sizeof(name), "mmcblk%d", i);
        char probe[64];
        if (sysfs_str(name, "type", probe, sizeof(probe)) ||
            sysfs_str(name, "cid", probe, sizeof(probe))) {
            snprintf(dev, cap, "%s", name);
            return true;
        }
    }
    return false;
}

static int read_health(const char *dev, const struct health_layout *lay,
                       uint8_t out[HEALTH_LEN], int *err) {
    char path[64];
    snprintf(path, sizeof(path), "/dev/%s", dev);

    int fd = open(path, O_RDWR);
    if (fd < 0) {
        *err = errno;
        return -1;
    }

    struct mmc_ioc_cmd_compat ic;
    memset(&ic, 0, sizeof(ic));
    ic.opcode = SD_GEN_CMD;
    ic.arg = lay->arg;
    ic.flags = MMC_RSP_SPI_S1 | MMC_RSP_R1 | MMC_CMD_ADTC;
    ic.blksz = HEALTH_LEN;
    ic.blocks = 1;
    ic.data_ptr = (uint64_t)(uintptr_t)out;
    /* Bounded, so a card that answers the command and then says nothing costs
     * half a second rather than the host's own default. */
    ic.data_timeout_ns = 500u * 1000u * 1000u;

    int rc = ioctl(fd, MMC_IOC_CMD_COMPAT, &ic);
    *err = rc < 0 ? errno : 0;
    close(fd);
    return rc;
}

static bool sig_matches(const struct health_layout *lay,
                        const uint8_t buf[HEALTH_LEN]) {
    for (size_t i = 0; i < sizeof(lay->sig) / sizeof(lay->sig[0]); i++)
        if (lay->sig[i] && !memcmp(buf, lay->sig[i], strlen(lay->sig[i])))
            return true;
    return false;
}

/* A run of printable ASCII, trimmed. The vendor string sits in the register
 * padded with spaces. */
static void ascii_at(const uint8_t *buf, size_t off, size_t len, char *out,
                     size_t cap) {
    size_t n = 0;
    for (size_t i = 0; i < len && n + 1 < cap; i++) {
        int c = buf[off + i];
        if (!isprint(c))
            break;
        out[n++] = (char)c;
    }
    while (n && out[n - 1] == ' ')
        n--;
    out[n] = 0;
}

static void usage(void) {
    printf("Usage: ipctool sdcard [--raw] [--json]\n\n"
           "What the SD card reports about itself, and its remaining life\n"
           "where the card implements the vendor health register (CMD56).\n\n"
           "  --raw     also dump the 512-byte register as hex\n"
           "  --json    print JSON instead of YAML\n");
}

int sdcard_cmd(int argc, char *argv[]) {
    bool raw = false, want_json = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage();
            return EXIT_SUCCESS;
        }
        if (!strcmp(argv[i], "--raw"))
            raw = true;
        else if (!strcmp(argv[i], "--json"))
            want_json = true;
    }

    char dev[16];
    if (!find_card(dev, sizeof(dev))) {
        fprintf(stderr, "No SD/MMC card found\n");
        return EXIT_FAILURE;
    }

    cJSON *j_root = cJSON_CreateObject();
    cJSON *j_inner = cJSON_CreateObject();
    cJSON_AddItemToObject(j_root, "sdcard", j_inner);

    ADD_PARAM("device", dev);

    char val[128];
    if (sysfs_str(dev, "name", val, sizeof(val)))
        ADD_PARAM("name", val);
    if (sysfs_str(dev, "manfid", val, sizeof(val)))
        ADD_PARAM("manfid", val);
    if (sysfs_str(dev, "oemid", val, sizeof(val)))
        ADD_PARAM("oemid", val);
    if (sysfs_str(dev, "serial", val, sizeof(val)))
        ADD_PARAM("serial", val);
    if (sysfs_str(dev, "date", val, sizeof(val)))
        ADD_PARAM("date", val);
    if (sysfs_str(dev, "cid", val, sizeof(val)))
        ADD_PARAM("cid", val);

    unsigned manfid = 0;
    if (sysfs_str(dev, "manfid", val, sizeof(val)))
        manfid = (unsigned)strtoul(val, NULL, 16);

    const struct health_layout *lay = layout_for(manfid);
    if (!lay) {
        /* Said out loud, because "no health section" and "this card has no
         * wear to report" are different facts and only the first is true. */
        ADD_PARAM("health",
                  "not read: this manufacturer's health register has not been "
                  "verified, and guessing the command can upset the card");
    } else {
        uint8_t buf[HEALTH_LEN];
        memset(buf, 0, sizeof(buf));
        int err = 0;
        if (read_health(dev, lay, buf, &err) < 0) {
            ADD_PARAM_FMT("health", "not read: %s", strerror(err));
        } else if (!sig_matches(lay, buf)) {
            /* The command succeeded and the reply is not the register. Saying
             * so beats decoding whatever came back. */
            ADD_PARAM("health",
                      "not read: the card answered CMD56 with something that "
                      "is not this vendor's health register");
        } else {
            cJSON *j_health = cJSON_CreateObject();
            cJSON *j_outer = j_inner;
            j_inner = j_health;

            ADD_PARAM("vendor", lay->vendor);
            ADD_PARAM_NUM("life_used_percent", buf[lay->life_used]);
            /* Host bytes are the camera's business; this is the card's own
             * account of itself, and it is still only a percentage of a rated
             * endurance the card never states. */
            ADD_PARAM("note", "percentage of rated life the card reports as "
                              "used; the rating itself is in no register");

            char text[64];
            ascii_at(buf, 0, 8, text, sizeof(text));
            if (text[0])
                ADD_PARAM("signature", text);
            ascii_at(buf, 0x31, 32, text, sizeof(text));
            if (text[0])
                ADD_PARAM("manufacturer", text);

            j_inner = j_outer;
            cJSON_AddItemToObject(j_inner, "health", j_health);
        }

        if (raw) {
            char hex[HEALTH_LEN * 2 + 1];
            for (int i = 0; i < HEALTH_LEN; i++)
                snprintf(hex + i * 2, 3, "%02x", buf[i]);
            ADD_PARAM("raw", hex);
        }
    }

    char *out = want_json ? cJSON_Print(j_root) : cYAML_Print(j_root);
    if (out) {
        printf("%s", out);
        free(out);
    }
    cJSON_Delete(j_root);
    return EXIT_SUCCESS;
}
