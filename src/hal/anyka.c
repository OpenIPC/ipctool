#include "hal/anyka.h"

#include <ctype.h>
#include <glob.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "chipid.h"
#include "hal/common.h"
#include "tools.h"

/* Addresses as the vendor's own sensor drivers spell them -- the 3.4 SDKs'
 * drivers/media/video/plat-anyka/ and the sensor modules on AK3918EV200,
 * EV300 and EV330 firmware -- which is the 8-bit write form ipctool's tables
 * use. SOI parts sit at either end: the JX-F2x/H63 drivers use 0x80, the
 * H42/H62/H65 ones 0x60. The OV9712 shares 0x60 with SmartSens, the GC1024
 * sits at 0x78 and the EV330's GC4653 at 0x52. */
static unsigned char sony_addrs[] = {0x34, 0};
static unsigned char soi_addrs[] = {0x80, 0x60, 0};
static unsigned char ssens_addrs[] = {0x60, 0x64, 0};
static unsigned char onsemi_addrs[] = {0x20, 0};
static unsigned char omni_addrs[] = {0x6c, 0x60, 0};
static unsigned char gc_addrs[] = {0x42, 0x6e, 0x78, 0x52, 0};
static unsigned char superpix_addrs[] = {0x7a, 0x78, 0};
static unsigned char imagedesign_addrs[] = {0x60, 0};
#ifndef STANDALONE_LIBRARY
static unsigned char primesensor_addrs[] = {0x90, 0};
static unsigned char byd_addrs[] = {0xdc, 0};
#endif

static sensor_addr_t anyka_possible_i2c_addrs[] = {
    {SENSOR_SONY, sony_addrs},
    {SENSOR_SOI, soi_addrs},
    {SENSOR_SMARTSENS, ssens_addrs},
    {SENSOR_ONSEMI, onsemi_addrs},
    {SENSOR_OMNIVISION, omni_addrs},
    {SENSOR_GALAXYCORE, gc_addrs},
    {SENSOR_SUPERPIX, superpix_addrs},
    {SENSOR_IMAGEDESIGN, imagedesign_addrs},
#ifndef STANDALONE_LIBRARY
    {SENSOR_PRIMESENSOR, primesensor_addrs},
    {SENSOR_BYD, byd_addrs},
#endif
    {0, NULL}};

/* Raw chip ID as it was read, kept so the report can show the number the
 * kernel's own "ANYKA CPU ... (ID 0x...)" banner prints. Zero when /dev/mem
 * would not give it up. */
static uint32_t anyka_chip_id;

static const struct {
    uint32_t id;
    /* The 2019 and later parts moved the ID up into bits 31:8 and match on
     * the top 24 bits only, leaving the low byte to vary; the mask falls out
     * of the shift. */
    uint32_t shift;
    int generation;
} anyka_socs[] = {
    /* The Aimer39 AK3918 and some AK3918EV200 boards share this one; the
     * board string or the device tree tells them apart. */
    {0x20120100, 0, AK39_EV2},
    {0x20150200, 0, AK39_EV2},
    {0x20160100, 0, AK39_EV3},
    {0x20160101, 0, AK39_EV330},
    {0x20170200, 0, AK37_D},
    {0x00201902, 8, AK37_E},
    {0x00535335, 8, AK39_AV100},
    {0x00535434, 8, AK39_EV300L},
    /* AK3918AV130, revisions A and B, read at AK_REG_CHIP_ID_AV. */
    {0x00565735, 8, AK39_AV130},
};

int anyka_generation(uint32_t chip_id) {
    for (size_t i = 0; i < ARRCNT(anyka_socs); i++) {
        uint32_t shift = anyka_socs[i].shift;

        if (((chip_id >> shift) & (0xFFFFFFFFu >> shift)) == anyka_socs[i].id)
            return anyka_socs[i].generation;
    }
    return 0;
}

/* These kernels predate device tree on the camera parts -- the 3.4.35 in
 * issue #134 has no /proc/device-tree at all -- and the SoC registers no UART
 * with /proc/iomem, so the machine string is the one signal that every
 * kernel gives without /dev/mem. Every Anyka camera board spells a part
 * number into it: "CLOUD39EV3_AK3918EV300_MNBD",
 * "Cloud39EV2_AK3918E80PIN_MNBD", "AK3918EV200_GWELL_V1", "Cloud39E_AK3918E+H42_V1.0.2",
 * "Aimer39_AK3918_MB_V1.0.0", "AK39EV330". */
bool anyka_machine_name(const char *cpuinfo, char *out, size_t outlen) {
    if (!line_from_file(cpuinfo, "Hardware.*:.*([Aa][Kk]3[0-9][0-9A-Za-z]*)",
                        out, outlen))
        return false;

    /* line_from_file() leaves no terminator behind when the match fills the
     * buffer, and this one gets copied on into chip_name. */
    out[outlen - 1] = '\0';
    return true;
}

/* Is s "AK" + four digits, e.g. AK3918? */
static bool is_part_base(const char *s) {
    return (s[0] | 32) == 'a' && (s[1] | 32) == 'k' && isdigit(s[2]) &&
           isdigit(s[3]) && isdigit(s[4]) && isdigit(s[5]);
}

static bool is_bare_part(const char *s) {
    return is_part_base(s) && !s[6];
}

/* ...followed by EVnnn or AVnnn: the whole part number, AK3918EV200. A
 * package, "AK3918E80PIN", has no V there. */
static bool is_full_part(const char *s) {
    return is_part_base(s) && s[6] && (s[7] | 32) == 'v' && isdigit(s[8]);
}

/* The model to report. `compat` is what follows "anyka," in the device tree's
 * root compatible, whose first entry is the most specific: the exact part
 * where the tree has one. Otherwise the board string names it when it carries
 * a whole part number. Where it carries only the package -- "AK3918E80PIN" is
 * an EV200 on one board and an EV300 on another -- the chip ID's generation
 * completes it, and its constant spells the suffix: 0x3918E200 is EV200.
 * Aimer39 has no suffix and the EV300L's constant spells none. EV330 boards
 * say only "AK39EV330", which is no part: that one is named as the vendor
 * kernel's CPU banner names it. */
void anyka_model_name(const char *machine, const char *compat, int generation,
                      char *out, size_t outlen) {
    const char *part = compat;

    if (!part || !is_full_part(part)) {
        part = machine ? machine : "";
        if (generation == AK39_EV330 && !is_full_part(part))
            part = "AK39XXEV330";
    }
    if (is_part_base(part) && !is_full_part(part) && generation >> 24 == 0x39 &&
        generation != AK39_EV1 && !(generation & 0xf))
        snprintf(out, outlen, "AK%.4s%XV%03X", part + 2, generation >> 12 & 0xf,
                 generation & 0xfff);
    else
        snprintf(out, outlen, "%s", part);
    for (char *p = out; *p; p++)
        *p = toupper((unsigned char)*p);
}

bool anyka_detect_cpu(char *chip_name) {
    /* As wide as chip_name, since that is where it lands. */
    char machine[128] = "", compat[32] = "";
    uint32_t reg;

    bool by_machine =
        anyka_machine_name("/proc/cpuinfo", machine, sizeof(machine));
    /* OpenIPC's kernels name the part in the device tree too; a board string
     * is then not needed to claim the SoC. */
    if (!line_from_file("/proc/device-tree/compatible", "^anyka,(.*)", compat,
                        sizeof(compat)) &&
        !by_machine)
        return false;
    compat[sizeof(compat) - 1] = '\0';

    /* Only ever a refinement: the board already named the part, and a kernel
     * built with strict devmem filtering can refuse the window outright. The
     * AV130 keeps its ID at the far end of the same block. */
    if (mem_reg(AK_REG_CHIP_ID, &reg, OP_READ)) {
        anyka_chip_id = reg;
        chip_generation = anyka_generation(reg);
        if (!chip_generation && mem_reg(AK_REG_CHIP_ID_AV, &reg, OP_READ) &&
            anyka_generation(reg) == AK39_AV130) {
            anyka_chip_id = reg;
            chip_generation = AK39_AV130;
        }
    }

    /* 0x20120100 is also the Aimer39 AK3918's, a generation of its own. Its
     * boards and device trees name a bare part, "Aimer39_AK3918_MB_V1.0.0"
     * and "anyka,ak3918", where an EV200's carry more. */
    if (anyka_chip_id == 0x20120100 &&
        (is_bare_part(machine) || is_bare_part(compat)))
        chip_generation = AK39_EV1;

    anyka_model_name(by_machine ? machine : NULL, compat, chip_generation,
                     chip_name, 128);
    return *chip_name != '\0';
}

#ifndef STANDALONE_LIBRARY
static size_t read_file(const char *path, char *buf, size_t len) {
    FILE *f = fopen(path, "rb");
    size_t n;

    if (!f)
        return 0;
    n = fread(buf, 1, len, f);
    fclose(f);
    return n;
}

/* Total DRAM is not something Linux can always see here. A vendor boot
 * loader keeps the foot of the bank for the ISP and the encoder and hands the
 * kernel only what is left -- 37 MB of a 64 MB part on the camera in issue
 * #134 -- and says how much there is in `memsize=`, which is why the SDK
 * reads it. OpenIPC's kernels take mem= alone and leave the rest of the
 * memory the device tree declares to the ISP. Either way /proc/iomem says
 * how much of it the kernel ended up with. */
static bool iomem_kernel_ram(const char *iomem, unsigned long *start,
                             unsigned long *end) {
    char buf[64];

    if (!line_from_file(iomem, "^(\\w+)-\\w+ *: *System RAM", buf,
                        sizeof(buf)))
        return false;
    *start = strtoul(buf, NULL, 16);

    if (!line_from_file(iomem, "^\\w+-(\\w+) *: *System RAM", buf,
                        sizeof(buf)))
        return false;
    *end = strtoul(buf, NULL, 16);
    return *end > *start;
}

unsigned long anyka_media_mem(const char *cmdline, const char *iomem) {
    char buf[256];
    unsigned long memsize, start, end;

    if (!line_from_file(cmdline, "memsize=([0-9]+)M", buf, sizeof(buf)))
        return 0;
    memsize = strtoul(buf, NULL, 10) * 1024;

    if (!iomem_kernel_ram(iomem, &start, &end))
        return 0;

    unsigned long kernel_ram = (end - start + 1) / 1024;
    return memsize > kernel_ram ? memsize - kernel_ram : 0;
}

/* The device tree's /memory, in kB: one <base size> pair of 32-bit
 * big-endian cells, as every AK39 device tree has it. ARM_ATAG_DTB_COMPAT
 * puts the boot loader's own figure there. */
unsigned long anyka_dt_memsize(const char *reg_path) {
    unsigned char cells[8];

    if (read_file(reg_path, (char *)cells, sizeof(cells)) != sizeof(cells))
        return 0;
    uint32_t size = (uint32_t)cells[4] << 24 | (uint32_t)cells[5] << 16 |
                    (uint32_t)cells[6] << 8 | cells[7];
    return size / 1024;
}

static unsigned long anyka_totalmem(unsigned long *media_mem) {
    unsigned long dt_total, start, end;

    *media_mem = anyka_media_mem("/proc/cmdline", "/proc/iomem");
    if (*media_mem)
        return *media_mem + kernel_mem();

    dt_total = anyka_dt_memsize("/proc/device-tree/memory/reg");
    if (dt_total && iomem_kernel_ram("/proc/iomem", &start, &end)) {
        unsigned long kernel_ram = (end - start + 1) / 1024;
        if (dt_total > kernel_ram) {
            *media_mem = dt_total - kernel_ram;
            return dt_total;
        }
    }
    return kernel_mem();
}

static void anyka_chip_properties(cJSON *j_inner) {
    /* getchipfamily() falls back to the model when the generation is unknown,
     * and repeating the model as its own family says nothing. */
    if (chip_generation)
        ADD_PARAM("family", getchipfamily());

    if (anyka_chip_id)
        ADD_PARAM_FMT("id", "0x%08x", anyka_chip_id);
}
#endif

#ifndef STANDALONE_LIBRARY
/*
 * The sensor answers I2C only while it is clocked and out of reset, and on
 * these SoCs that is only while the camera driver is using it: it powers the
 * sensor up to probe it and down again afterwards. So the probe powers it up
 * itself, the way the drivers do -- the AK3918EV200 and the Cloud39EV300
 * SDK's ak_camera.c and OpenIPC's ak-cam.c alike: the CIS clock from the
 * peripheral PLL in SYSCTRL 0x18 (divider in bits 15:10, latched by bit 19,
 * enabled by bit 18; 24 MHz, which every sensor these SoCs drive accepts),
 * then a reset pulse on the board's reset and power-down GPIOs, which only a
 * device tree names. Measured on an AK3918EV200 (SC1345): the sensor reads
 * 0xffff before and its chip ID after.
 *
 * Nothing is touched when the clock is already running: then a pipeline may
 * be streaming, the sensor already answers, and a reset would stop it. What
 * is touched is put back by anyka_hal_cleanup(), bit for bit, and only that.
 */
#define AK_REG_PERI_PLL1 (AK_PA_SYSCTRL + 0x14)
#define AK_REG_PERI_PLL2 (AK_PA_SYSCTRL + 0x18)
#define AK_PA_GPIO 0x20170000
#define AK_GPIO_DIR(pin) (AK_PA_GPIO + 0x00 + ((pin) >> 5) * 4)
#define AK_GPIO_OUT(pin) (AK_PA_GPIO + 0x0c + ((pin) >> 5) * 4)
#define AK_CIS_MCLK_MHZ 24

struct anyka_pin {
    int pin;    /* -1: none */
    bool active_low;
    bool saved_dir, saved_out;
};

static struct {
    bool armed;
    uint32_t pll1, pll2;
    struct anyka_pin reset, pwdn;
} anyka_power;

/* <phandle pin flags> of a camera node's reset-gpio or pwdn-gpio. The
 * pattern must match one node: with several, which camera's pins these are
 * is not known, and none is touched. The AK39 device trees have one GPIO
 * controller, so the phandle is not followed; flags bit 0 is
 * GPIO_ACTIVE_LOW. */
bool anyka_dt_gpio(const char *pattern, int *pin, bool *active_low) {
    glob_t g;
    unsigned char cells[12];
    bool ok = false;

    if (glob(pattern, 0, NULL, &g))
        return false;
    if (g.gl_pathc == 1 &&
        read_file(g.gl_pathv[0], (char *)cells, sizeof(cells)) == 12) {
        *pin = cells[6] << 8 | cells[7];
        *active_low = cells[11] & 1;
        ok = *pin < 128;
    }
    globfree(&g);
    return ok;
}

static bool gpio_bit(uint32_t reg, int pin, bool *val) {
    uint32_t v;

    if (!mem_reg(reg, &v, OP_READ))
        return false;
    *val = v >> (pin & 31) & 1;
    return true;
}

static void gpio_set_bit(uint32_t reg, int pin, bool val) {
    uint32_t v;

    if (!mem_reg(reg, &v, OP_READ))
        return;
    v = val ? v | 1u << (pin & 31) : v & ~(1u << (pin & 31));
    mem_reg(reg, &v, OP_WRITE);
}

/* The camera nodes under the I2C adapter being probed: its device tree node
 * is what /sys/bus/i2c/devices/i2c-N/of_node points at. A kernel that does
 * not link it leaves the glob over every adapter, which anyka_dt_gpio()
 * accepts only when it finds a single camera. */
static void camera_pattern(char *out, size_t len, const char *prop) {
    char link[64], node[256];
    const char *base = "/sys/firmware/devicetree/base/";
    ssize_t n;

    snprintf(link, sizeof(link), "/sys/bus/i2c/devices/i2c-%d/of_node",
             i2c_adapter_nr);
    n = readlink(link, node, sizeof(node) - 1);
    if (n > 0) {
        node[n] = '\0';
        char *rel = strstr(node, "devicetree/base/");
        if (rel) {
            snprintf(out, len, "%s%s/camera@*/%s", base,
                     rel + strlen("devicetree/base/"), prop);
            return;
        }
    }
    snprintf(out, len, "/proc/device-tree/soc/i2c*/camera@*/%s", prop);
}

static void claim_pin(struct anyka_pin *p, const char *prop) {
    char pattern[320];

    camera_pattern(pattern, sizeof(pattern), prop);
    if (!anyka_dt_gpio(pattern, &p->pin, &p->active_low) ||
        !gpio_bit(AK_GPIO_DIR(p->pin), p->pin, &p->saved_dir) ||
        !gpio_bit(AK_GPIO_OUT(p->pin), p->pin, &p->saved_out))
        p->pin = -1;
}

/* Drive a pin to its asserted (on) or released (off) level. */
static void drive_pin(struct anyka_pin *p, bool assert) {
    if (p->pin < 0)
        return;
    gpio_set_bit(AK_GPIO_OUT(p->pin), p->pin, assert == p->active_low ? 0 : 1);
    gpio_set_bit(AK_GPIO_DIR(p->pin), p->pin, 1);
}

static void restore_pin(struct anyka_pin *p) {
    if (p->pin < 0)
        return;
    gpio_set_bit(AK_GPIO_DIR(p->pin), p->pin, p->saved_dir);
    gpio_set_bit(AK_GPIO_OUT(p->pin), p->pin, p->saved_out);
}

/* Peripheral PLL in MHz, as the camera drivers compute it, or 0 when the
 * register holds something they would refuse. */
unsigned anyka_peri_pll_mhz(uint32_t pll1) {
    unsigned m = pll1 & 0xff, n = pll1 >> 8 & 0xf, od = pll1 >> 12 & 0x3;

    if (od < 1 || n < 2 || n > 6 || m < 84 || m > 254)
        return 0;
    return 12 * m / (n * (1u << od));
}

static void anyka_enable_sensor_clock() {
    uint32_t pll1, pll2, v;
    unsigned pll;

    if (anyka_power.armed)
        return;
    if (chip_generation != AK39_EV2 && chip_generation != AK39_EV3)
        return;
    if (!mem_reg(AK_REG_PERI_PLL1, &pll1, OP_READ) ||
        !mem_reg(AK_REG_PERI_PLL2, &pll2, OP_READ))
        return;
    if (pll2 & 1u << 18)
        return; /* running: leave it to whoever runs it */
    pll = anyka_peri_pll_mhz(pll1);
    if (!pll)
        return;

    /* The probe calls hal_cleanup() only after it has opened the bus, so a
     * power-up for a bus that will not open would never be undone. */
    int fd = open_i2c_sensor_fd(i2c_adapter_nr);
    if (fd < 0)
        return;
    close_sensor_fd(fd);

    anyka_power.pll1 = pll1;
    anyka_power.pll2 = pll2;
    anyka_power.armed = true;

    claim_pin(&anyka_power.reset, "reset-gpio");
    claim_pin(&anyka_power.pwdn, "pwdn-gpio");

    v = pll1 & ~(1u << 25);
    if (v != pll1)
        mem_reg(AK_REG_PERI_PLL1, &v, OP_WRITE);
    v = (pll2 & ~(0x3fu << 10)) | (pll / AK_CIS_MCLK_MHZ - 1) << 10 | 1u << 19;
    mem_reg(AK_REG_PERI_PLL2, &v, OP_WRITE);
    v |= 1u << 18;
    mem_reg(AK_REG_PERI_PLL2, &v, OP_WRITE);

    drive_pin(&anyka_power.pwdn, false);
    drive_pin(&anyka_power.reset, true);
    usleep(10000);
    drive_pin(&anyka_power.reset, false);
    usleep(20000);
}

static void anyka_hal_cleanup() {
    if (anyka_power.armed) {
        restore_pin(&anyka_power.reset);
        restore_pin(&anyka_power.pwdn);
        mem_reg(AK_REG_PERI_PLL2, &anyka_power.pll2, OP_WRITE);
        mem_reg(AK_REG_PERI_PLL1, &anyka_power.pll1, OP_WRITE);
        anyka_power.armed = false;
    }
    restore_printk();
}

#endif /* !STANDALONE_LIBRARY */

void anyka_setup_hal() {
    possible_i2c_addrs = anyka_possible_i2c_addrs;
#ifndef STANDALONE_LIBRARY
    hal_enable_sensor_clock = anyka_enable_sensor_clock;
    hal_cleanup = anyka_hal_cleanup;
    hal_totalmem = anyka_totalmem;
    hal_chip_properties = anyka_chip_properties;
#endif
}
