/* The AK3918AV100-family GPIO controller, as the vendor's
 * drivers/pinctrl/anyka/ak_3918av100_pinctrl.c drives it: at 0x20170000,
 * banks of four words -- DIR at +0x00, OUT at +0x14, IN at +0x28 -- and a
 * pad's bit is its number modulo 32 in word number / 32.
 *
 * The block has no set/clear aliases, so a level change is a read-modify-
 * write of the whole word, as the kernel does it (under local_irq_save
 * there, under the single-threaded contract of mem_reg() here).
 *
 * Which pads exist is the pad-mux table's to say: the EV300L package bonds
 * out 59 of the AV100's 93. */

#include "hal/anyka_gpio.h"

#include <dirent.h>
#include <glob.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "chipid.h"
#include "hal/anyka.h"
#include "ipchw.h"
#include "tools.h"

bool anyka_gpio_supported(void) {
    return chip_generation == AK39_AV100 || chip_generation == AK39_EV300L;
}

bool anyka_gpio_valid(int pad) {
    if (!anyka_gpio_supported() || pad < 0 || pad >= AK_GPIO_NPADS)
        return false;
    ipchw_padmux_t row;
    return ipchw_padmux_by_pad(pad, &row, 1) > 0;
}

uint32_t anyka_gpio_reg(int pad, unsigned bank) {
    if (!anyka_gpio_valid(pad) ||
        (bank != AK_GPIO_DIR && bank != AK_GPIO_OUT && bank != AK_GPIO_IN))
        return 0;
    return AK_GPIO_BASE + bank + (uint32_t)(pad >> 5) * 4;
}

bool anyka_gpio_window(uint32_t *base, uint32_t *len) {
    if (!anyka_gpio_supported())
        return false;
    *base = AK_GPIO_BASE;
    *len = AK_GPIO_IN + 4 * ((AK_GPIO_NPADS + 31) / 32);
    return true;
}

bool anyka_gpio_read(int pad, bool *output, bool *level) {
    uint32_t dir, data;
    if (!anyka_gpio_valid(pad) ||
        !mem_reg(anyka_gpio_reg(pad, AK_GPIO_DIR), &dir, OP_READ))
        return false;
    uint32_t bit = 1u << (pad & 31);
    *output = (dir & bit) != 0;
    if (!mem_reg(anyka_gpio_reg(pad, *output ? AK_GPIO_OUT : AK_GPIO_IN),
                 &data, OP_READ))
        return false;
    *level = (data & bit) != 0;
    return true;
}

bool anyka_gpio_write(int pad, bool level) {
    uint32_t out, dir;
    uint32_t bit = 1u << (pad & 31);
    if (!anyka_gpio_valid(pad) ||
        !mem_reg(anyka_gpio_reg(pad, AK_GPIO_OUT), &out, OP_READ))
        return false;
    out = level ? out | bit : out & ~bit;
    if (!mem_reg(anyka_gpio_reg(pad, AK_GPIO_OUT), &out, OP_WRITE) ||
        !mem_reg(anyka_gpio_reg(pad, AK_GPIO_DIR), &dir, OP_READ))
        return false;
    if (dir & bit)
        return true;
    dir |= bit;
    return mem_reg(anyka_gpio_reg(pad, AK_GPIO_DIR), &dir, OP_WRITE);
}

static const char *root = "";

/* Who holds each pad, read once per process: the device-tree walk is a few
 * hundred files, and `gpio scan` asks about every pad. */
static char owners[AK_GPIO_NPADS][64];
static bool owners_read;

void anyka_gpio_set_root(const char *dir) {
    root = dir ? dir : "";
    owners_read = false;
}

static void copy_name(char *who, size_t len, const char *name) {
    if (len == 0)
        return;
    snprintf(who, len, "%s", name);
}

static void set_owner(int pin, const char *name) {
    if (pin >= 0 && pin < AK_GPIO_NPADS && !owners[pin][0])
        snprintf(owners[pin], sizeof owners[pin], "%.63s", name);
}

/* "pin 23 (gpio23): 20300000.ethernet (GPIO UNCLAIMED) function ..." */
static void read_pinmux_owners(void) {
    char pattern[256];
    snprintf(pattern, sizeof pattern,
             "%s/sys/kernel/debug/pinctrl/*/pinmux-pins", root);
    glob_t g;
    if (glob(pattern, 0, NULL, &g) != 0)
        return;
    for (size_t i = 0; i < g.gl_pathc; i++) {
        FILE *f = fopen(g.gl_pathv[i], "r");
        if (!f)
            continue;
        char line[256];
        while (fgets(line, sizeof line, f)) {
            int pin;
            char owner[128];
            /* "(MUX UNCLAIMED)" is nobody. */
            if (sscanf(line, "pin %d (%*[^)]): %127s", &pin, owner) == 2 &&
                owner[0] != '(')
                set_owner(pin, owner);
        }
        fclose(f);
    }
    globfree(&g);
}

/* " gpio-18  (                    |sensor-reset        ) out hi" */
static void read_gpiolib_owners(void) {
    char path[256];
    snprintf(path, sizeof path, "%s/sys/kernel/debug/gpio", root);
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        int pin;
        const char *bar = strchr(line, '|');
        char consumer[128];
        if (sscanf(line, " gpio-%d", &pin) != 1 || !bar ||
            sscanf(bar + 1, "%127[^)]", consumer) != 1)
            continue;
        size_t n = strlen(consumer);
        while (n > 0 && consumer[n - 1] == ' ')
            consumer[--n] = '\0';
        /* The user's own /sys/class/gpio export is not the kernel's. */
        if (n > 0 && strcmp(consumer, "sysfs") != 0)
            set_owner(pin, consumer);
    }
    fclose(f);
}

static bool node_enabled(const char *dir) {
    char path[512], status[16] = "";
    snprintf(path, sizeof path, "%s/status", dir);
    FILE *f = fopen(path, "rb");
    if (!f)
        return true; /* no status is "okay" */
    size_t n = fread(status, 1, sizeof status - 1, f);
    fclose(f);
    status[n] = '\0';
    return !strcmp(status, "okay") || !strcmp(status, "ok");
}

static bool ends_with(const char *s, const char *tail) {
    size_t a = strlen(s), b = strlen(tail);
    return a >= b && !strcmp(s + a - b, tail);
}

/* Fills owners[] from every *-gpio / *-gpios property of an enabled node:
 * <phandle pin flags> big-endian cells. The AK39 device trees have one GPIO
 * controller, so the phandle is not followed; 0xffff is the vendor's "no
 * pin". */
static void dt_walk(const char *dir, int depth) {
    if (depth > 8 || !node_enabled(dir))
        return;
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        char path[512];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(path, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            dt_walk(path, depth + 1);
            continue;
        }
        if (!ends_with(e->d_name, "-gpio") && !ends_with(e->d_name, "-gpios"))
            continue;
        FILE *f = fopen(path, "rb");
        if (!f)
            continue;
        unsigned char cells[12 * 8];
        size_t n = fread(cells, 1, sizeof cells, f);
        fclose(f);
        for (size_t i = 0; i + 12 <= n; i += 12) {
            uint32_t pin = (uint32_t)cells[i + 4] << 24 |
                           (uint32_t)cells[i + 5] << 16 |
                           (uint32_t)cells[i + 6] << 8 | cells[i + 7];
            /* 0xffff is the vendor's "no pin"; it is past the table too. A
             * pad debugfs already named keeps that name. */
            if (pin >= AK_GPIO_NPADS || owners[pin][0])
                continue;
            const char *node = strrchr(dir, '/');
            char name[160];
            snprintf(name, sizeof name, "%s %s", node ? node + 1 : dir,
                     e->d_name);
            set_owner((int)pin, name);
        }
    }
    closedir(d);
}

static void read_owners(void) {
    memset(owners, 0, sizeof owners);
    read_pinmux_owners();
    read_gpiolib_owners();
    char dt[256];
    snprintf(dt, sizeof dt, "%s/proc/device-tree", root);
    dt_walk(dt, 0);
    owners_read = true;
}

bool anyka_gpio_owner(int pad, char *who, size_t len) {
    if (pad < 0 || pad >= AK_GPIO_NPADS)
        return false;
    if (!owners_read)
        read_owners();
    if (!owners[pad][0])
        return false;
    copy_name(who, len, owners[pad]);
    return true;
}
