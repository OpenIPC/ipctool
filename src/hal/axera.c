#include "hal/axera.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "chipid.h"
#include "hal/common.h"
#include "tools.h"

static unsigned char sony_addrs[] = {0x34, 0};
static unsigned char ssens_addrs[] = {0x60, 0};
static unsigned char omni_addrs[] = {0x6c, 0};

static sensor_addr_t axera_possible_i2c_addrs[] = {
    {SENSOR_SONY, sony_addrs},
    {SENSOR_SMARTSENS, ssens_addrs},
    {SENSOR_OMNIVISION, omni_addrs},
    {0, NULL}};

/*
 * AX620E family (AX620Q, AX630C, AX620QX, ...): the model is fused at the
 * factory. The SDK's BL1 (boot/bl1/board/board.c get_misc_info()) reads
 * eFuse block 2 (CHIP_BOND_BLK) and takes bits 31:24 as the chip type,
 * Hamming(7,4)-encoded; store_chip_type() decodes it, turns 0 (an unfused
 * part) into AX630C and writes the result to COMMON_SYS_GLB DUMMY_SW9.
 * efuse_read() (boot/bl1/driver/secure/efuse_drv.c) is a plain load of the
 * controller's shadow register, EFSC0_BASE + EFSC_SHADOW_0 + blk * 4, with
 * no read sequence, so /dev/mem sees the same word BL1 does.
 */
#define AX620E_EFSC0_SHADOW_BLK2 (0x02200000 + 0x58 + 2 * 4)
#define AX620E_DUMMY_SW9 (0x02340000 + 0x220)

/* boot/bl1/board/include/boot_mode.h chip_type_e. Inline arrays, not
 * pointers: a pointer table costs a relocation per entry in ipcinfo. */
static const char ax620e_names[][10] = {
    "ax620e", "ax620q",  "ax620qx", "ax620v200", "ax630c",
    "ax631", "ax620qz", "ax620qp", "ax620qe",   "ax620qf",
};
#define AX630C_CHIP 4

/*
 * AX620U family (AX620U, AX620A, AX620D): the stock firmware's own
 * ax_hwinfo.ko (ax_chip_type_proc_show(), /proc/ax_proc/chip_type) names
 * the part from two registers. ax_sys.ko's ax_ddr_bits_read() is bit 24 of
 * the DDR controller's 0x00102068: clear on AX620U, set on the parts with
 * the wider DDR bus. Those are AX620D when every bit of 0x191800 is set in
 * the bond option register 0x0222001C, AX620A otherwise. 0x00102068 is a
 * setting the SPL programs for the board's DRAM (0x00010000 on a stock
 * AX620U), not a fuse, but it is the vendor's own test.
 */
#define AX620U_DDRMC_2068 0x00102068
#define AX620U_DDR_BITS (1u << 24)
#define AX620U_BOND_OPT 0x0222001C
#define AX620U_BOND_AX620D 0x00191800

/* hanming_decode_74() from board.c: bits 6:3 carry the type, bits 2:0 the
 * parity; a single flipped bit is corrected the way BL1 corrects it. The
 * code is systematic, so on a clean fuse the type is bits 6:3 verbatim, and
 * that is all ipcinfo, held to its size budget, reads; ipctool corrects. */
static int axera_hamming74_decode(uint8_t code) {
#ifndef STANDALONE_LIBRARY
    /* column[bit] is the syndrome a flip of that bit gives */
    static const uint8_t column[7] = {1, 2, 4, 3, 5, 6, 7};
    uint8_t syndrome = 0;
    int bit;

    for (bit = 0; bit < 7; bit++)
        if (code & (1 << bit))
            syndrome ^= column[bit];
    /* syndromes 3, 5, 6, 7 name data bits 3..6; 1, 2, 4 are parity */
    if (syndrome & (syndrome - 1))
        code ^= 1 << (syndrome == 3 ? 3 : syndrome - 1);
#endif
    return (code >> 3) & 0x0f;
}

/* The eFuse decides; DUMMY_SW9 only answers when the eFuse word reads 0,
 * which is also what a shadow register closed to the normal world looks
 * like. Both 0 is an unfused part, which BL1 boots as AX630C. */
static const char *axera_ax620e_name(uint32_t efuse_blk2, uint32_t dummy_sw9) {
    uint8_t code = efuse_blk2 >> 24;
    int type;

    if (code)
        type = axera_hamming74_decode(code);
    else if (dummy_sw9 && dummy_sw9 < ARRCNT(ax620e_names))
        type = dummy_sw9;
    else
        type = 0;
    if (!type)
        type = AX630C_CHIP;
    /* A type the SDK does not list is named by its family, entry 0. */
    return ax620e_names[type < (int)ARRCNT(ax620e_names) ? type : 0];
}

static const char *axera_ax620u_name(uint32_t ddrmc_2068, uint32_t bond_opt) {
    if (!(ddrmc_2068 & AX620U_DDR_BITS))
        return "ax620u";
    if ((bond_opt & AX620U_BOND_AX620D) == AX620U_BOND_AX620D)
        return "ax620d";
    return "ax620a";
}

/* Names the part from its registers. Without /dev/mem (mem_reg fails for
 * both words alike) the family name is the answer: AX620U's rule gives it
 * for the zeros left unread, AX620E's needs the check. The sensor bus is the
 * one the boards' own devicetrees and sensor libraries use: AX620U's on i2c8
 * (OS04A10 at 0x36 on the JFtech A8 board), AX620E's on i2c6 (SC450AI at
 * 0x30 on the Xiongmai IVG-A6, i2c@4856000); the fallback opener takes the
 * number, so the sweep in sensors.c still reaches the other buses. */
static bool axera_identify(char *chip_name, bool e) {
    static const uint32_t regs[2][2] = {
        {AX620U_DDRMC_2068, AX620U_BOND_OPT},
        {AX620E_EFSC0_SHADOW_BLK2, AX620E_DUMMY_SW9}};
    uint32_t a = 0, b = 0;
    bool read = mem_reg(regs[e][0], &a, OP_READ);

    mem_reg(regs[e][1], &b, OP_READ);
    chip_generation = e ? AXERA_AX620E : AXERA_AX620U;
    i2c_adapter_nr = e ? 6 : 8;
    strcpy(chip_name, !e     ? axera_ax620u_name(a, b)
                      : read ? axera_ax620e_name(a, b)
                             : ax620e_names[0]);
    return true;
}

/*
 * The root compatible only says which family's register map this is
 * ("axera,ax620e", or "axera,ax620x" on AX620U) -- every AX620E-family part
 * says "axera,ax620e" -- so it gates the register reads and the registers
 * name the part. The property is a list of NUL-separated strings and a
 * board string usually comes first ("xm,ipc", "axera,ax620e"). Read with
 * open/read, which ipcinfo already imports, rather than stdio's fread.
 */
bool axera_detect_cpu(char *chip_name) {
    char compat[512];
    int fd = open("/proc/device-tree/compatible", O_RDONLY), len;
    const char *c;

    if (fd < 0)
        return false;
    len = read(fd, compat, sizeof(compat) - 1);
    close(fd);
    if (len <= 0)
        return false;
    compat[len] = '\0';

    /* "axera,ax620e" or "axera,ax620x", the whole entry */
    for (c = compat; c < compat + len; c += strlen(c) + 1)
        if (!strncmp(c, "axera,ax620", 11) && (c[11] == 'e' || c[11] == 'x') &&
            !c[12])
            return axera_identify(chip_name, c[11] == 'e');
    return false;
}

void axera_setup_hal() {
    possible_i2c_addrs = axera_possible_i2c_addrs;
}
