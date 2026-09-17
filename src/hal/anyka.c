#include "hal/anyka.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "chipid.h"
#include "hal/common.h"
#include "tools.h"

/* Addresses as the vendor's own sensor drivers in
 * drivers/media/video/plat-anyka/ spell them, which is the 8-bit write form
 * ipctool's tables use. SOI parts sit at either end: the JX-F2x/H63 drivers
 * use 0x80 and the H65 one 0x60. */
static unsigned char sony_addrs[] = {0x34, 0};
static unsigned char soi_addrs[] = {0x80, 0x60, 0};
static unsigned char ssens_addrs[] = {0x60, 0};
static unsigned char onsemi_addrs[] = {0x20, 0};
static unsigned char omni_addrs[] = {0x6c, 0};
static unsigned char gc_addrs[] = {0x42, 0x6e, 0};
static unsigned char superpix_addrs[] = {0x7a, 0x78, 0};

static sensor_addr_t anyka_possible_i2c_addrs[] = {
    {SENSOR_SONY, sony_addrs},         {SENSOR_SOI, soi_addrs},
    {SENSOR_SMARTSENS, ssens_addrs},   {SENSOR_ONSEMI, onsemi_addrs},
    {SENSOR_OMNIVISION, omni_addrs},   {SENSOR_GALAXYCORE, gc_addrs},
    {SENSOR_SUPERPIX, superpix_addrs}, {0, NULL}};

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
    {0x20120100, 0, AK39_EV2},
    {0x20150200, 0, AK39_EV2},
    {0x20160100, 0, AK39_EV3},
    {0x20160101, 0, AK39_EV330},
    {0x20170200, 0, AK37_D},
    {0x00201902, 8, AK37_E},
    {0x00535335, 8, AK39_AV100},
    {0x00535434, 8, AK39_EV300L},
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
 * with /proc/iomem, so the machine string is the only signal that does not
 * need /dev/mem. Every Anyka camera board spells the part into it:
 * "CLOUD39EV3_AK3918EV300_MNBD", "Cloud39EV2_AK3918E80PIN_MNBD",
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

bool anyka_detect_cpu(char *chip_name) {
    /* As wide as chip_name, since that is where it lands. */
    char machine[128];
    uint32_t reg;

    if (!anyka_machine_name("/proc/cpuinfo", machine, sizeof(machine)))
        return false;
    strcpy(chip_name, machine);

    /* Only ever a refinement: the board already named the part, and a kernel
     * built with strict devmem filtering can refuse the window outright. */
    if (mem_reg(AK_REG_CHIP_ID, &reg, OP_READ)) {
        anyka_chip_id = reg;
        chip_generation = anyka_generation(reg);
    }

    return true;
}

#ifndef STANDALONE_LIBRARY
/* Total DRAM is not something Linux can see here. The boot loader keeps the
 * foot of the bank for the ISP and the encoder and hands the kernel only what
 * is left -- 37 MB of a 64 MB part on the camera in issue #134 -- so
 * /proc/meminfo is short by exactly the media reservation. The vendor boot
 * argument `memsize` carries the real size, which is why the SDK reads it,
 * and /proc/iomem says how much of it the kernel ended up with. */
unsigned long anyka_media_mem(const char *cmdline, const char *iomem) {
    char buf[256];
    unsigned long memsize, start, end;

    if (!line_from_file(cmdline, "memsize=([0-9]+)M", buf, sizeof(buf)))
        return 0;
    memsize = strtoul(buf, NULL, 10) * 1024;

    if (!line_from_file(iomem, "^(\\w+)-\\w+ *: *System RAM", buf,
                        sizeof(buf)))
        return 0;
    start = strtoul(buf, NULL, 16);

    if (!line_from_file(iomem, "^\\w+-(\\w+) *: *System RAM", buf,
                        sizeof(buf)))
        return 0;
    end = strtoul(buf, NULL, 16);

    if (end <= start)
        return 0;

    unsigned long kernel_ram = (end - start + 1) / 1024;
    return memsize > kernel_ram ? memsize - kernel_ram : 0;
}

static unsigned long anyka_totalmem(unsigned long *media_mem) {
    *media_mem = anyka_media_mem("/proc/cmdline", "/proc/iomem");
    return *media_mem + kernel_mem();
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

void anyka_setup_hal() {
    possible_i2c_addrs = anyka_possible_i2c_addrs;
#ifndef STANDALONE_LIBRARY
    hal_totalmem = anyka_totalmem;
    hal_chip_properties = anyka_chip_properties;
#endif
}
