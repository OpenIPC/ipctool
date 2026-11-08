#include "hal/anyka.h"

#include <ctype.h>
#include <fcntl.h>
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
#ifndef STANDALONE_LIBRARY
static uint32_t anyka_chip_id;
#endif

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
 * "Cloud39EV2_AK3918E80PIN_MNBD", "AK3918EV200_GWELL_V1",
 * "Cloud39E_AK3918E+H42_V1.0.2",
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

#ifndef STANDALONE_LIBRARY
/* Is this match of the board or device tree patterns below "AK" + four
 * digits, e.g. AK3918, and not the generation-wide AK39EV330? */
static bool is_part_base(const char *s) {
    return s[0] && isdigit(s[4]) && isdigit(s[5]);
}

static bool is_bare_part(const char *s) {
    return is_part_base(s) && !s[6];
}

/* ...followed by EVnnn or AVnnn: the whole part number, AK3918EV200. A
 * package, "AK3918E80PIN", has no V there. */
static bool is_full_part(const char *s) {
    return is_part_base(s) && s[6] && (s[7] | 32) == 'v';
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
    const char *part = is_full_part(compat) ? compat : machine;

    if (generation == AK39_EV330 && !is_full_part(part))
        part = "AK39XXEV330";
    if (generation && !(generation & 0xf) && is_part_base(part) &&
        !is_full_part(part))
        snprintf(out, outlen, "AK%.4s%XV%03X", part + 2, generation >> 12 & 0xf,
                 generation & 0xfff);
    else
        snprintf(out, outlen, "%s", part);
    for (char *p = out; *p; p++)
        *p = toupper((unsigned char)*p);
}

/* Up to len bytes of a file, or 0. */
static int read_file(const char *path, void *buf, size_t len) {
    int fd = open(path, O_RDONLY), n;

    if (fd < 0)
        return 0;
    n = read(fd, buf, len);
    close(fd);
    return n;
}

/* Everything a vendor kernel's board string leaves to be worked out. */
static bool anyka_full_model(char *chip_name, uint32_t id) {
    /* As wide as chip_name, since that is where it lands. */
    char machine[128], compat[32];

    machine[0] = compat[0] = '\0';
    anyka_machine_name("/proc/cpuinfo", machine, sizeof(machine));
    line_from_file("/proc/device-tree/compatible", "^anyka,(ak.*)", compat,
                   sizeof(compat));
    compat[sizeof(compat) - 1] = '\0';

    /* 0x20120100 is also the Aimer39 AK3918's, a generation of its own. Its
     * boards and device trees name a bare part, "Aimer39_AK3918_MB_V1.0.0"
     * and "anyka,ak3918", where an EV200's carry more. */
    if (id == 0x20120100 && (is_bare_part(machine) || is_bare_part(compat)))
        chip_generation = AK39_EV1;

    anyka_model_name(machine, compat, chip_generation, chip_name, 128);
    return *chip_name != '\0';
}
#endif

bool anyka_detect_cpu(char *chip_name) {
    uint32_t reg = 0;

    /* OpenIPC's kernels name the exact part in the device tree, so a board
     * string is then not needed to claim the SoC. */
    if (!line_from_file("/proc/device-tree/compatible",
                        "^anyka,(ak[0-9]{4}[ae]v.*)", chip_name, 128) &&
        !anyka_machine_name("/proc/cpuinfo", chip_name, 128))
        return false;

    /* Only ever a refinement: the board already named the part, and a kernel
     * built with strict devmem filtering can refuse the window outright. */
    if (mem_reg(AK_REG_CHIP_ID, &reg, OP_READ))
        chip_generation = anyka_generation(reg);

#ifndef STANDALONE_LIBRARY
    /* The AV130 keeps its ID at the far end of the same block. */
    uint32_t av;
    if (reg && !chip_generation && mem_reg(AK_REG_CHIP_ID_AV, &av, OP_READ) &&
        anyka_generation(av) == AK39_AV130) {
        reg = av;
        chip_generation = AK39_AV130;
    }
    anyka_chip_id = reg;
    return anyka_full_model(chip_name, reg);
#else
    return true;
#endif
}

#ifndef STANDALONE_LIBRARY
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

    if (read_file(reg_path, cells, sizeof(cells)) != (int)sizeof(cells))
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
 * ipcinfo, in every image, starts the clock alone: the sensor drivers' power
 * off leaves those pins as inputs, not held asserted, and the SC1345 answers
 * with just the clock. ipctool also pulses the pins.
 *
 * Nothing is touched when the clock is already running: then a pipeline may
 * be streaming, the sensor already answers, and a reset would stop it. What
 * is touched is put back by anyka_hal_cleanup(), and only that.
 */
#define AK_REG_PERI_PLL1 (AK_PA_SYSCTRL + 0x14)
#define AK_REG_PERI_PLL2 (AK_PA_SYSCTRL + 0x18)
#define AK_GPIO_DIR 0x20170000
#define AK_GPIO_OUT 0x2017000c

/* The CIS clock divider field for 24 MHz from the peripheral PLL register,
 * 12 * M / (N * 2^OD) MHz over 24, minus one; 0 for a register the camera
 * drivers would refuse. Counted up to keep division out of the binary. */
unsigned anyka_cis_divider(uint32_t pll1) {
    unsigned m = pll1 & 0xff, n = pll1 >> 8 & 0xf, od = pll1 >> 12 & 0x3, d = 0;

    if (!od || n - 2 > 4)
        return 0;
    while ((2 * (d + 2) * n << od) <= m)
        d++;
    return d;
}

/* What each register held before the power-up first wrote it: the PLL's two
 * and, in ipctool, direction and level for each of a camera's two pins.
 * Restored in one write each: replaying every write to the PLL register left
 * its latch bit set on an AK3918EV200. */
#ifdef STANDALONE_LIBRARY
#define AK_UNDO_MAX 2
#else
#define AK_UNDO_MAX 6
#endif
static struct {
    uint32_t addr, val;
} anyka_undo[AK_UNDO_MAX];
static int anyka_undo_n;

static void anyka_rmw(uint32_t addr, uint32_t clear, uint32_t set) {
    uint32_t v = 0;

    mem_reg(addr, &v, OP_READ);
    v = (v & ~clear) | set;
    mem_reg(addr, &v, OP_WRITE);
}

/* anyka_rmw() for a register's first write, remembering what it held. */
static void anyka_poke(uint32_t addr, uint32_t clear, uint32_t set) {
    anyka_undo[anyka_undo_n].addr = addr;
    mem_reg(addr, &anyka_undo[anyka_undo_n++].val, OP_READ);
    anyka_rmw(addr, clear, set);
}

#ifndef STANDALONE_LIBRARY
/* The camera nodes under the I2C adapter being probed: its device tree node
 * is what /sys/bus/i2c/devices/i2c-N/of_node points at. A kernel that does
 * not link it leaves the glob over every adapter, which is used only when it
 * finds a single camera. */
static void camera_gpio_pattern(char *out, size_t len) {
    char link[64], node[256];
    const char *rel;
    ssize_t n;

    snprintf(link, sizeof(link), "/sys/bus/i2c/devices/i2c-%d/of_node",
             i2c_adapter_nr);
    n = readlink(link, node, sizeof(node) - 1);
    if (n > 0) {
        node[n] = '\0';
        rel = strstr(node, "devicetree/base/");
        if (rel) {
            snprintf(out, len, "/sys/firmware/%s/camera@*/*-gpio", rel);
            return;
        }
    }
    snprintf(out, len, "/proc/device-tree/soc/i2c*/camera@*/*-gpio");
}

/* <phandle pin flags> of each GPIO property of the camera node; flags bit 0
 * is GPIO_ACTIVE_LOW. The AK39 device trees have one GPIO controller, so the
 * phandle is not followed. More matches than one camera has -- whose pins
 * they are is then not known -- and none is touched. Power down is released
 * and reset asserted, then reset released. */
static void anyka_pulse_camera_pins(void) {
    uint32_t rst_reg = 0, rst_bit = 0, rst_val = 0;
    char pattern[320];
    glob_t g;

    camera_gpio_pattern(pattern, sizeof(pattern));
    if (glob(pattern, 0, NULL, &g))
        return;
    for (size_t i = 0; g.gl_pathc <= 2 && i < g.gl_pathc; i++) {
        unsigned char c[12];

        if (read_file(g.gl_pathv[i], c, sizeof(c)) != sizeof(c) || c[7] > 127)
            continue;
        uint32_t bank = (c[7] >> 5) * 4, bit = 1u << (c[7] & 31);
        bool low = c[11] & 1, reset = strstr(g.gl_pathv[i], "reset");

        anyka_poke(AK_GPIO_OUT + bank, bit, reset != low ? bit : 0);
        anyka_poke(AK_GPIO_DIR + bank, 0, bit);
        if (reset) {
            rst_reg = AK_GPIO_OUT + bank;
            rst_bit = bit;
            rst_val = low ? bit : 0;
        }
    }
    globfree(&g);
    usleep(10000);
    if (rst_reg)
        anyka_rmw(rst_reg, rst_bit, rst_val);
}
#endif

static void anyka_enable_sensor_clock() {
    uint32_t pll1, pll2;
    unsigned div;

    if (anyka_undo_n || (chip_generation | 0x100) != AK39_EV3 ||
        !mem_reg(AK_REG_PERI_PLL1, &pll1, OP_READ) ||
        !mem_reg(AK_REG_PERI_PLL2, &pll2, OP_READ) ||
        pll2 & 1u << 18) /* running: leave it to whoever runs it */
        return;
    div = anyka_cis_divider(pll1);
    if (!div)
        return;

    /* The probe calls hal_cleanup() only after it has opened the bus, so a
     * power-up for a bus that will not open would never be undone. */
    int fd = open_i2c_sensor_fd(i2c_adapter_nr);
    if (fd < 0)
        return;
    close_sensor_fd(fd);

    if (pll1 & 1u << 25)
        anyka_poke(AK_REG_PERI_PLL1, 1u << 25, 0);
    anyka_poke(AK_REG_PERI_PLL2, 0x3fu << 10, div << 10 | 1u << 19);
    anyka_rmw(AK_REG_PERI_PLL2, 0, 1u << 18);

#ifndef STANDALONE_LIBRARY
    anyka_pulse_camera_pins();
#endif
    usleep(20000);
}

static void anyka_hal_cleanup() {
    while (anyka_undo_n) {
        anyka_undo_n--;
        mem_reg(anyka_undo[anyka_undo_n].addr, &anyka_undo[anyka_undo_n].val,
                OP_WRITE);
    }
    restore_printk();
}

void anyka_setup_hal() {
    possible_i2c_addrs = anyka_possible_i2c_addrs;
    hal_enable_sensor_clock = anyka_enable_sensor_clock;
    hal_cleanup = anyka_hal_cleanup;
#ifndef STANDALONE_LIBRARY
    hal_totalmem = anyka_totalmem;
    hal_chip_properties = anyka_chip_properties;
#endif
}
