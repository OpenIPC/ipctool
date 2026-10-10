#!/usr/bin/env python3
"""Generate src/hal/novatek_padmux.h from a Novatek NA51089 SDK.

WHAT THE HARDWARE DOES

A NA51089 pad carries a peripheral when two things agree:

  * a per-PERIPHERAL field in the TOP block (0xF0010000 + 0x00..0x24) whose
    value picks which group of pads the peripheral uses -- REG5[7:6] is I2C3,
    and 1, 2, 3 put it on P_GPIO21/22, C_GPIO11/12 or DSI_GPIO8/9;
  * a per-PAD gate bit in one of eight bitmaps at TOP + 0xA0..0xE8, where 1
    keeps the pad a GPIO and 0 hands it to whichever function claims it.

Not every claim needs the gate. The parallel and CCIR sensor modes put pixel
data on the HSI pads, and the vendor driver writes GPIO_ID_EMUM_GPIO to their
gate bits while doing it. Those claims are "ungated": they hold whatever the
gate says.

WHERE THE TABLE COMES FROM

There is no datasheet and no pin list in the SDK. The truth is the vendor's
kernel driver, drivers/pinctrl/novatek/na51089/na51089_pinmux_host.c, and in
it the pinmux_config_<group>() functions: one per peripheral group, each
taking a bitmask of PIN_<GROUP>_CFG_* options and writing fields and gate
bits. They are 2200 lines of nested if/else-if/switch with conflict checks
interleaved -- too irregular to read with patterns, and too easy to misread
by hand. So this RUNS them:

  1. The config functions and the globals they use are cut out of host.c,
     every `top_regN.bit.FIELD = value;` is wrapped in a macro that logs the
     register, the field's mask, the value and which assignment it was, and
     the result is compiled on the host against the SDK's own top_reg.h,
     top.h and na51089_pinmux.h, with empty stand-ins for the Linux headers.
  2. Every function is called with every combination of up to four of its
     option bits, from the state pinmux_init() leaves: every field 0, every
     gate bit 1. A call that returns an error is a combination the vendor
     refuses, and is dropped.
  3. The source is read for one thing only: which braces enclose each
     assignment, and which options the `if (config & ...)` or `case` that
     opens each of them names. A gate write's CONDITIONS are the field
     writes the same call made in the blocks enclosing it: SEN_MCLK=1 for
     S_GPIO0 under PIN_SENSOR_CFG_MCLK, and not the sensor mode the same call
     happened to select in a sibling block.

A pad's NAME comes from the `@PAD[NAME]|...` and `&&PAD[NAME]` annotations on
the option enums in top.h, looked up for the enclosing options innermost
first, then from build/nvt-tools/nvt_pinctrl_tool/top.csv, which is the only
source for SDIO and NAND. A pad nothing names gets its option's own name --
SENSOR_12BITS, MIPI_LVDS_CLK0, ETH_RMII -- which names a bus rather than a
wire, the way SigmaStar's I2C1_MODE_3 does.

WHAT IS CHECKED

src/hal/novatek_padmux.c answers "what is this pad" by walking the pad's
claims in table order and taking the first whose conditions all hold and
which is ungated or has its gate bit clear. Claims are sorted most conditions
first. That rule is replayed here over every state the vendor code produced:

  * a pad the call gave a function must read as that function -- unless a
    later write in the same call moved its field away (MCLK_2ND after MCLK
    leaves S_GPIO0 cleared and claimed by nothing), in which case it must
    read as nothing;
  * a pad the call left alone must not read as anything;
  * where two claims hold at once the vendor code says which one it meant,
    by the gate write that came last, and a claim specific to that overlap
    is added: ETH_RMII on DSI_GPIO9 when ETH_MDIO is also set.

Every annotation is also checked against the code: a pad an annotation names
for an option whose code never touches it is a disagreement between two
vendor sources, fatal unless ANNOTATION_ERRATA says which one is right.

USAGE
    tools/gen_novatek_padmux.py --sdk <na51089 SDK root> > src/hal/novatek_padmux.h
    tools/gen_novatek_padmux.py --sdk <root> --verify src/hal/novatek_padmux.h
    tools/gen_novatek_padmux.py --selftest

Needs a host C compiler (cc, or $CC).
"""

import argparse
import collections
import difflib
import os
import re
import shutil
import subprocess
import sys
import tempfile

# The groups, in the vendor's pinmux_config_hdl[] order, with the enum type
# of each one's options. LCD and TV are not here: they only stash their
# argument for pinmux_select_primary_lcd() and write no register.
FUNCS = [
    ("sdio", "PIN_SDIO_CFG"),
    ("sdio2", "PIN_SDIO_CFG"),
    ("sdio3", "PIN_SDIO_CFG"),
    ("nand", "PIN_NAND_CFG"),
    ("sensor", "PIN_SENSOR_CFG"),
    ("sensor2", "PIN_SENSOR2_CFG"),
    ("mipi_lvds", "PIN_MIPI_LVDS_CFG"),
    ("i2c", "PIN_I2C_CFG"),
    ("sif", "PIN_SIF_CFG"),
    ("uart", "PIN_UART_CFG"),
    ("spi", "PIN_SPI_CFG"),
    ("sdp", "PIN_SDP_CFG"),
    ("remote", "PIN_REMOTE_CFG"),
    ("pwm", "PIN_PWM_CFG"),
    ("pwm2", "PIN_PWM_CFG2"),
    ("ccnt", "PIN_CCNT_CFG"),
    ("audio", "PIN_AUDIO_CFG"),
    ("eth", "PINMUX_ETH_CFG"),
    ("misc", "PINMUX_MISC_CFG"),
    # Not a pinmux_config_*(): pinmux_select_primary_lcd(), which is what the
    # display driver calls through pinmux_set_host(). Its argument is one LCD
    # type plus feature flags, so it is run as each type with every set of
    # flags rather than as combinations of bits. TV and HDMI have no pads to
    # mux on these parts: pinmux_set_host() answers E_ID for both.
    ("lcd", "PINMUX_LCD_SEL"),
]

# The function a FUNCS entry calls, when it is not pinmux_config_<name>().
SPECIAL = {"lcd": "pinmux_select_primary_lcd"}

# The drivers this runs, and the SoCs each is run as. The na51055 driver
# serves two dies and branches on nvt_get_chip_id() -- NA51084 refuses UART2's
# 3rd location, for one -- so it is run once as each, and each gets a table.
# The third field says whether build/nvt-tools/.../top.csv describes it: the
# csv is the NA51089 tool's.
FAMILIES = [
    ("na51089", [("NA51089", 0x7021)], True),
    ("na51055", [("NA51055", 0x4821), ("NA51084", 0x5021)], False),
]

# The groups whose GPIO writes are claims rather than releases. sensor and
# sensor2 write GPIO_ID_EMUM_GPIO to the pads they put pixel data on; every
# other GPIO write gives a pad back (SPI's DI-only mode releases CLK, DO and
# CS that way).
UNGATED_FUNCS = {"sensor", "sensor2"}

# The most options combined in one call. UART2's DIR/OE on its 2nd location
# is CH2 | CH2_2ND | CH2_DIROE; a fourth leaves room for one more on top.
MAX_COMBO = 4

# Pad groups: (gate register offset, kernel prefix, Linux GPIO base, the
# nvt-gpio.h macro saying how many are bonded out). A pad's gate register is
# GATE_REGS[pad >> 5]; src/hal/novatek_padmux.c relies on that.
GROUPS = [
    (0xA0, "C_GPIO", 0x00, "C_GPIO_NUM"),
    (0xA8, "P_GPIO", 0x20, "P_GPIO_NUM"),
    (0xB0, "S_GPIO", 0x40, "S_GPIO_NUM"),
    (0xB8, "L_GPIO", 0x60, "L_GPIO_NUM"),
    (0xD0, "D_GPIO", 0x80, "D_GPIO_NUM"),
    (0xD8, "H_GPIO", 0xA0, "H_GPIO_NUM"),
    (0xE0, "A_GPIO", 0xC0, "A_GPIO_NUM"),
    (0xE8, "DSI_GPIO", 0xE0, "DSI_GPIO_NUM"),
]
GATE_REGS = {g[0]: g for g in GROUPS}

# How the two name sources spell a pad, and the kernel macro it is.
PAD_SPELLINGS = [
    (re.compile(r"^MC(\d+)$"), "C_GPIO"),
    (re.compile(r"^C_GPIO(\d+)$"), "C_GPIO"),
    (re.compile(r"^P_GPIO(\d+)$"), "P_GPIO"),
    (re.compile(r"^S_GPIO(\d+)$"), "S_GPIO"),
    (re.compile(r"^L_GPIO(\d+)$"), "L_GPIO"),
    (re.compile(r"^D_GPIO(\d+)$"), "D_GPIO"),
    (re.compile(r"^(?:HSI_GPIO|H_GPIO)(\d+)$"), "H_GPIO"),
    (re.compile(r"^A_GPIO(\d+)$"), "A_GPIO"),
    (re.compile(r"^DSI_GPIO(\d+)$"), "DSI_GPIO"),
]

# Where the two vendor sources disagree with the code, and what was decided.
# Key: (family, option, pad as the source spells it). Value: the (option,
# pad) it means, or None to drop the annotation.
ANNOTATION_ERRATA = {
    # top.csv only; top.h has P_GPIO19, and so does the code.
    ("na51089", "PIN_SIF_CFG_CH2_2ND_PINMUX", "P_GPIO29"):
        ("PIN_SIF_CFG_CH2_2ND_PINMUX", "P_GPIO19"),
    # top.h only; the code and top.csv both put PICNT2_1 on L_GPIO1. On
    # NA51055 the same annotation is right: its code uses P_GPIO14.
    ("na51089", "PIN_PWM_CFG_CCNT2", "P_GPIO14"):
        ("PIN_PWM_CFG_CCNT2", "L_GPIO1"),
    # pinmux_config_misc() tests PIN_SENSOR_CFG_SP2CLK_3RD where it means
    # this option, so this option does nothing at all. The same pad and field
    # are reached through the sensor group's SP2CLK_3RD, which top.h does not
    # annotate; the name goes there.
    ("na51089", "PIN_MISC_CFG_SP2CLK_3RD", "D_GPIO4"):
        ("PIN_SENSOR_CFG_SP2CLK_3RD", "D_GPIO4"),

    # NA51055's top.h carries NA51089's MISC annotations; its own code puts
    # the special clocks elsewhere, and has no 3rd SP_CLK location at all.
    ("na51055", "PIN_MISC_CFG_SPCLK", "P_GPIO17"):
        ("PIN_MISC_CFG_SPCLK", "L_GPIO20"),
    ("na51055", "PIN_MISC_CFG_SPCLK_2ND", "L_GPIO23"):
        ("PIN_MISC_CFG_SPCLK_2ND", "P_GPIO19"),
    ("na51055", "PIN_MISC_CFG_SPCLK_3RD", "D_GPIO3"): None,
    # As on NA51089, only the sensor group reaches SP_CLK2's 3rd location.
    ("na51055", "PIN_MISC_CFG_SP2CLK_3RD", "D_GPIO4"):
        ("PIN_SENSOR_CFG_SP2CLK_3RD", "D_GPIO4"),
    ("na51055", "PIN_MISC_CFG_SP2CLK", "P_GPIO18"):
        ("PIN_MISC_CFG_SP2CLK", "P_GPIO24"),
    ("na51055", "PIN_MISC_CFG_SP2CLK_2ND", "MC9"):
        ("PIN_MISC_CFG_SP2CLK_2ND", "P_GPIO15"),
    # "52x compatible - donot support this function": the driver comments
    # out the HSI location of PWM8..11 on NA51055.
    ("na51055", "PIN_PWM_CFG_PWM8_3", "HSI_GPIO6"): None,
    ("na51055", "PIN_PWM_CFG_PWM9_3", "HSI_GPIO7"): None,
    ("na51055", "PIN_PWM_CFG2_PWM10_3", "HSI_GPIO8"): None,
    ("na51055", "PIN_PWM_CFG2_PWM11_3", "HSI_GPIO9"): None,
}

# One annotation: PAD[NAME]. The csv has unclosed brackets and stray
# parentheses; both are tolerated, and a name that still is not an
# identifier afterwards is refused.
ANNOT = re.compile(r"([A-Za-z_]+[0-9]+)\s*\[([^\]|,@&]+)\]?")


def die(msg):
    raise SystemExit("gen_novatek_padmux: " + msg)


def norm_name(raw):
    name = raw.strip().upper().replace("(BS)", "").rstrip(")").strip()
    if not re.match(r"^[A-Z][A-Z0-9_]*$", name):
        die("cannot use %r as a function name" % raw)
    return name


def norm_pad(raw):
    raw = raw.strip().upper()
    for rx, prefix in PAD_SPELLINGS:
        m = rx.match(raw)
        if m:
            return "%s%d" % (prefix, int(m.group(1)))
    return None


def pad_number(name):
    for _, prefix, base, _ in GROUPS:
        m = re.match(r"^%s(\d+)$" % prefix, name)
        if m:
            return base + int(m.group(1))
    die("not a pad: %s" % name)


def option_label(option):
    """PIN_SENSOR_CFG_12BITS -> SENSOR_12BITS, PINMUX_LCD_SEL_CCIR656 ->
    LCD_CCIR656."""
    m = re.match(r"^PINMUX_LCD_SEL_(\w+)$", option)
    if m:
        return "LCD_%s" % m.group(1)
    m = re.match(r"^PIN_(\w+?)_CFG2?_(\w+)$", option)
    if not m:
        die("cannot make a name of %s" % option)
    return "%s_%s" % (m.group(1), m.group(2))


# ---------------------------------------------------------------- sources


class Sources:
    """The SDK files this reads, located from the SDK root."""

    def __init__(self, sdk, family="na51089"):
        kernel = os.path.join(sdk, "BSP", "linux-kernel")
        self.family = family
        self.drv = os.path.join(kernel, "drivers", "pinctrl", "novatek",
                                family)
        self.host_c = os.path.join(self.drv, "%s_pinmux_host.c" % family)
        self.plat = os.path.join(kernel, "arch", "arm", "plat-novatek",
                                 "include", "plat-%s" % family)
        self.mach = os.path.join(kernel, "arch", "arm", "mach-nvt-ivot",
                                 "include", "mach")
        self.top_h = os.path.join(self.plat, "top.h")
        self.gpio_h = os.path.join(self.plat, "nvt-gpio.h")
        self.csv = os.path.join(sdk, "build", "nvt-tools",
                                "nvt_pinctrl_tool", "top.csv")
        for path in (self.host_c, self.top_h, self.gpio_h,
                     os.path.join(self.mach, "rcw_macro.h")):
            if not os.path.isfile(path):
                die("not a %s SDK: no %s" % (family.upper(), path))
        if not os.path.isfile(self.csv):
            self.csv = None


def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def parse_enums(text):
    """enum type -> [(member, value or None, trailing comment)]."""
    out = {}
    for m in re.finditer(r"typedef\s+enum\s*\{(.*?)\}\s*(\w+)\s*;", text,
                         re.S):
        members = []
        for line in m.group(1).splitlines():
            mm = re.match(r"\s*([A-Z][A-Z0-9_]*)\s*(?:=\s*([^,/]+?))?\s*,"
                          r"\s*(.*)$", line)
            if not mm or mm.group(1).startswith("ENUM_DUMMY"):
                continue
            try:
                val = int(mm.group(2), 0) if mm.group(2) else None
            except ValueError:
                val = None
            members.append((mm.group(1), val, mm.group(3)))
        out[m.group(2)] = members
    return out


def option_bits(enums, enum_name):
    """The options of one group that are a bit of their own, in order. For
    the LCD selector: its types, then its feature flags."""
    if enum_name not in enums:
        die("top.h has no enum %s" % enum_name)
    if enum_name == "PINMUX_LCD_SEL":
        types, flags = lcd_options(enums)
        return types + flags
    seen = set()
    bits = []
    for name, val, _ in enums[enum_name]:
        if val and val not in seen:
            seen.add(val)
            bits.append(name)
    return bits


def lcd_options(enums):
    """PINMUX_LCD_SEL's types -- the members with implicit values, which
    count from 0 -- and its feature flags, the ones with explicit values.
    A mask is neither."""
    types, flags = [], []
    for name, val, _ in enums["PINMUX_LCD_SEL"]:
        if name.endswith("_MSK"):
            continue
        (types if val is None else flags).append(name)
    return types, flags


def annotations(enums, csv_text, family="na51089"):
    """{(func or None, option, pad): name} and the list of what was found.

    top.h annotations hold for every group that takes the option; the csv's
    may be filed under one controller, because SDIO, SDIO2 and SDIO3 share
    their option names and the csv lists each under a sub-row."""
    names = {}
    found = []

    def add(func, option, pad_raw, name_raw):
        key = (family, option, pad_raw.strip().upper())
        if key in ANNOTATION_ERRATA:
            if ANNOTATION_ERRATA[key] is None:
                return
            option, pad_raw = ANNOTATION_ERRATA[key]
        pad = norm_pad(pad_raw)
        if pad is None:
            return  # "LCD0~LCD10": a range, not a pad
        names.setdefault((func, option, pad), norm_name(name_raw))
        found.append((option, pad))

    for members in enums.values():
        for option, _, comment in members:
            if option.startswith("PIN_"):
                for part in re.split(r"@|&&", comment)[1:]:
                    for m in ANNOT.finditer(part):
                        add(None, option, m.group(1), m.group(2))

    option = sub = None
    for row in (csv_text or "").splitlines():
        cells = row.split(",", 2)
        if cells[0].startswith("PIN_"):
            option, sub = cells[0], None
        elif cells[0] == "" and len(cells) > 2 and option:
            sub = {"SDIO1": "sdio", "SDIO2": "sdio2",
                   "SDIO3": "sdio3"}.get(cells[1].strip())
        else:
            option = None
            continue
        for m in ANNOT.finditer(cells[2] if len(cells) > 2 else ""):
            pad = norm_pad(m.group(1))
            if pad is not None and (None, option, pad) in names:
                continue  # top.h is the cleaner of the two and wins
            add(sub, option, m.group(1), m.group(2))
    return names, found


def bonded_pads(gpio_h_text):
    pads = []
    for _, prefix, _, macro in GROUPS:
        m = re.search(r"#define\s+%s\s+(\d+)" % macro, gpio_h_text)
        if not m:
            continue  # NA51055 has no DSI group
        pads.extend("%s%d" % (prefix, i) for i in range(int(m.group(1))))
    return pads


# ---------------------------------------------------------------- the code

ASSIGN = re.compile(r"\b(top_reg\w+)\.bit\.(\w+)\s*=(?!=)\s*([^;]+);")
OTHER_WRITE = re.compile(
    r"\btop_reg\w+\.(?:bit\.\w+|reg)\s*(?:[-+*/&|^]|<<|>>)?=(?!=)")
CASE = re.compile(r"\bcase\s+(PIN(?:MUX)?_\w+)\s*:")

# The options a block's header tests: `config & PIN_X`, `pinmux &
# (PIN_A | PIN_B)`. Only a bit test names a block's option. A range test --
# `(pinmux_type >= PINMUX_LCD_SEL_CCIR656) && (pinmux_type <= ...)` -- names
# the ends of a range, and an `==` test against the group's NONE opens an
# empty block.
TESTED = re.compile(r"(?<!&)&\s*\(?\s*(PIN(?:MUX)?_\w+"
                    r"(?:\s*\|\s*PIN(?:MUX)?_\w+)*)")


def tested(header):
    out = []
    for m in TESTED.finditer(header):
        out.extend(re.findall(r"PIN(?:MUX)?_\w+", m.group(1)))
    return out

Rec = collections.namedtuple("Rec", "func path chain")


def blank(text):
    """Comments and string literals as spaces, offsets unchanged -- a brace
    in a pr_err() message is not a block."""
    def spaces(m):
        return re.sub(r"[^\n]", " ", m.group(0))
    return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:[^"\\\n]|\\.)*"', spaces, text,
                  flags=re.S)


def extract_function(text, name):
    m = re.search(r"^static \w+ %s\([^)]*\)\s*\{" % name, text, re.M)
    if not m:
        die("host.c has no %s()" % name)
    clean = blank(text)
    depth = 0
    for i in range(m.end() - 1, len(text)):
        if clean[i] == "{":
            depth += 1
        elif clean[i] == "}":
            depth -= 1
            if depth == 0:
                return text[m.start():i + 1]
    die("%s() never closes" % name)


def instrument(func, body, recs):
    """`body` with every field assignment logged, and recs[] extended with
    where each one sits: the blocks around it, and the options that open
    them innermost first."""
    clean = blank(body)
    tokens = []
    for i, ch in enumerate(clean):
        if ch in "{}":
            tokens.append((i, ch, None))
    for m in CASE.finditer(clean):
        tokens.append((m.start(), "case", m.group(1)))
    for m in ASSIGN.finditer(clean):
        tokens.append((m.start(), "assign", m))
    tokens.sort(key=lambda t: t[0])

    stack = []    # [block start, options in its header, current case]
    boundary = 0  # where the current statement started
    sites = {}
    for pos, kind, arg in tokens:
        if kind == "{":
            header = clean[boundary:pos]
            stack.append([pos, tested(header), None])
            boundary = pos + 1
        elif kind == "}":
            if not stack:
                die("%s(): unbalanced braces" % func)
            stack.pop()
            boundary = pos + 1
        elif kind == "case":
            if stack:
                stack[-1][2] = arg
        else:
            chain = []
            for _, opts, case in reversed(stack):
                if case:
                    chain.append(case)
                chain.extend(opts)
            sites[pos] = Rec(func, tuple((func, s[0]) for s in stack),
                             tuple(chain))
            boundary = arg.end()

    out = []
    last = 0
    for m in ASSIGN.finditer(clean):
        recs.append(sites[m.start()])
        out.append(body[last:m.start()])
        out.append("REC(%s, %s, %s, %d);" % (m.group(1), m.group(2),
                                             body[m.start(3):m.end(3)],
                                             len(recs) - 1))
        last = m.end()
    out.append(body[last:])
    body = "".join(out)

    leftover = OTHER_WRITE.findall(blank(body))
    if leftover:
        die("%s() writes a register a way this does not log: %s"
            % (func, leftover[0]))
    return body


PRELUDE = r"""
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
typedef uint64_t u64; typedef int64_t s64; typedef uint32_t u32;
typedef int32_t s32; typedef uint16_t u16; typedef int16_t s16;
typedef uint8_t u8; typedef int8_t s8;
typedef struct { int unused; } spinlock_t;
struct device; struct seq_file;
#define __iomem
#include FAMILY_PINMUX_H
#undef pr_err
#undef TOP_SETREG
#define TOP_SETREG(...) ((void)0)
#define pr_debug(...) ((void)0)
#ifndef BIT
#define BIT(n) (1u << (n))
#endif
/* Which die the driver believes it is on. */
#define nvt_get_chip_id() ((uint32_t)NVT_CHIP)
#define pr_err(...) ((void)0)
#define pr_info(...) ((void)0)
#define pr_warn(...) ((void)0)
#define spin_lock_irqsave(l, f) ((void)(f))
#define spin_unlock_irqrestore(l, f) ((void)(f))
#define pad_set_pull_updown(p, t) ((void)0)

static void rec(const void *reg, uint32_t mask, uint32_t val, int id);
#define REC(R, F, V, ID) do { \
    __typeof__(R) m_; memset(&m_, 0, sizeof m_); m_.bit.F = -1; \
    (R).bit.F = (V); rec(&(R), m_.reg, (R).reg & m_.reg, ID); \
} while (0)
"""


def lcd_body(host):
    """pinmux_select_primary_lcd() with its local copies of REG2 and the L and
    DSI gates turned into the globals every other function writes: it reads
    them with TOP_GETREG() into locals, edits those, and writes them back
    with TOP_SETREG(), which is the same writes once the copies are gone."""
    body = extract_function(host, SPECIAL["lcd"])
    body = re.sub(r"^\s*union\s+TOP_\w+\s+local_\w+\s*;\s*$", "", body,
                  flags=re.M)
    body = re.sub(r"^\s*local_\w+\.reg\s*=\s*TOP_GETREG\([^;]*\);\s*$", "",
                  body, flags=re.M)
    return body.replace("local_top_reg", "top_reg")


def build_harness(src, enums, recs):
    host = read(src.host_c)
    start = host.index('#include "%s_pinmux.h"' % src.family)
    start = host.index("\n", start) + 1
    end = host.index("struct nvt_pinctrl_info info_get_id")
    region = host[start:end]

    unions = re.findall(r"^union\s+(TOP_REG\w+)\s+(top_reg\w+)\s*;", region,
                        re.M)
    if not unions:
        die("host.c declares no TOP register globals")
    statics = re.findall(
        r"^(?:static\s+)?(?:uint32_t|int|u32)\s+(\w+)\s*(?:\[[^\]]*\])*\s*=",
        region, re.M)

    c = [PRELUDE, region]
    # Every handler in the vendor's table has to exist for it to compile,
    # LCD and TV included.
    for name in re.findall(r"^static int (pinmux_config_\w+)\(uint32_t "
                           r"config\);", region, re.M):
        func = name[len("pinmux_config_"):]
        c.append(instrument(func, extract_function(host, name), recs))
    if any(f == "lcd" for f, _ in FUNCS):
        c.append(instrument("lcd", lcd_body(host), recs))
        c.append("static int lcd_select(uint32_t cfg) {\n"
                 "    return pinmux_select_primary_lcd(NULL, "
                 "PINMUX_DISPMUX_SEL_LCD, cfg);\n}")
    if not recs:
        die("found no register writes to log")

    c.append("static const struct { const void *p; uint32_t ofs; } "
             "REGS[] = {")
    for utype, var in unions:
        c.append("    {&%s, %s_OFS}," % (var, utype))
    c.append("};")
    c.append(r"""
static char buf[1 << 16];
static size_t blen;

static void rec(const void *reg, uint32_t mask, uint32_t val, int id) {
    const char *fmt = "W ? %x %x %d\n";
    uint32_t ofs = 0;
    for (size_t i = 0; i < sizeof REGS / sizeof *REGS; i++)
        if (REGS[i].p == reg) {
            fmt = "W %x %x %x %d\n";
            ofs = REGS[i].ofs;
        }
    if (fmt[2] == '?')
        blen += snprintf(buf + blen, sizeof buf - blen, fmt, mask, val, id);
    else
        blen += snprintf(buf + blen, sizeof buf - blen, fmt, ofs, mask, val,
                         id);
    if (blen >= sizeof buf - 64) {
        fprintf(stderr, "log overflow\n");
        _exit(1);
    }
}
""")
    # Every call starts from the same globals.
    c.append("static void reset(void) {")
    c.append("    static int saved;")
    for i, var in enumerate(statics):
        c.append("    static unsigned char s%d[sizeof %s];" % (i, var))
    c.append("    if (!saved) {")
    for i, var in enumerate(statics):
        c.append("        memcpy(s%d, &%s, sizeof %s);" % (i, var, var))
    c.append("        saved = 1;")
    c.append("    }")
    for i, var in enumerate(statics):
        c.append("    memcpy(&%s, s%d, sizeof %s);" % (var, i, var))
    for utype, var in unions:
        c.append("    %s.reg = %s;" % (
            var, "0xFFFFFFFFu" if "GPIO" in utype else "0"))
    c.append("}")

    nfuncs = len(FUNCS)
    for fi, (func, enum_name) in enumerate(FUNCS):
        c.append("static const uint32_t bits%d[] = {%s};"
                 % (fi, ", ".join(option_bits(enums, enum_name))))
    c.append("static int (*const fns[])(uint32_t) = {%s};" % ", ".join(
        "lcd_select" if f == "lcd" else "pinmux_config_%s" % f
        for f, _ in FUNCS))
    lcd = [i for i, (f, _) in enumerate(FUNCS) if f == "lcd"]
    ntypes = len(lcd_options(enums)[0]) if lcd else 0
    c.append("static const int lcd_fi = %d, lcd_ntypes = %d;"
             % (lcd[0] if lcd else -1, ntypes))
    c.append("static const uint32_t *const bits[] = {%s};" % ", ".join(
        "bits%d" % i for i in range(nfuncs)))
    c.append("static const int nbits[] = {%s};" % ", ".join(
        "sizeof bits%d / sizeof *bits%d" % (i, i) for i in range(nfuncs)))
    c.append(r"""
/* One combination of one group: on its own, or -- when the vendor refuses
 * it on its own -- after the first single option of another group that
 * makes it acceptable. MIPI data lanes 1-3 want the sensor group in CSI
 * mode first, and that is the only way to see them. */
static void one(int fi, const int *idx, int k) {
    uint32_t cfg = 0;
    for (int i = 0; i < k; i++)
        cfg |= bits[fi][idx[i]];
    reset();
    blen = 0;
    int ret = fns[fi](cfg);
    int pg = -1, pb = -1;
    for (int g = 0; ret != 0 && g < %d; g++) {
        if (g == fi)
            continue;
        for (int b = 0; ret != 0 && b < nbits[g]; b++) {
            reset();
            blen = 0;
            if (fns[g](bits[g][b]) != 0)
                continue;
            ret = fns[fi](cfg);
            pg = g;
            pb = b;
        }
    }
    if (ret != 0)
        return;
    printf("R %%d", fi);
    for (int i = 0; i < k; i++)
        printf(" %%d", idx[i]);
    printf("\n");
    if (pg >= 0)
        printf("P %%d %%d\n", pg, pb);
    fwrite(buf, 1, blen, stdout);
    printf("E 0\n");
}
""" % nfuncs)
    c.append("int main(void) {")
    for utype, var in unions:
        c.append('    printf("U %%x %s\\n", %s_OFS);' % (var, utype))
    c.append("""    for (int fi = 0; fi < %d; fi++) {
        const int n = nbits[fi];
        int idx[32];
        if (fi == lcd_fi) {
            /* One type, any set of flags. */
            const int nf = n - lcd_ntypes;
            for (int t = 0; t < lcd_ntypes; t++)
                for (int fl = 0; fl < (1 << nf); fl++) {
                    int k = 0;
                    idx[k++] = t;
                    for (int b = 0; b < nf; b++)
                        if (fl & (1 << b))
                            idx[k++] = lcd_ntypes + b;
                    one(fi, idx, k);
                }
            continue;
        }
        for (int k = 1; k <= %d && k <= n; k++) {
            for (int i = 0; i < k; i++) idx[i] = i;
            for (;;) {
                one(fi, idx, k);
                int i = k - 1;
                while (i >= 0 && idx[i] == n - k + i) i--;
                if (i < 0) break;
                idx[i]++;
                for (int j = i + 1; j < k; j++) idx[j] = idx[j - 1] + 1;
            }
        }
    }""" % (nfuncs, MAX_COMBO))
    c.append("    return 0;")
    c.append("}")
    return "\n".join(c) + "\n"


SHIM_EMPTY = ["linux/types.h", "linux/irq.h", "linux/spinlock.h",
              "linux/slab.h", "linux/of.h", "asm/types.h", "mach/nvt-io.h"]


def run_harness(src, enums, recs, chip_id):
    cc = os.environ.get("CC", "cc")
    tmp = tempfile.mkdtemp(prefix="nvt_padmux_")
    try:
        shim = os.path.join(tmp, "shim")
        for rel in SHIM_EMPTY:
            path = os.path.join(shim, rel)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            open(path, "w").close()
        for name in ("rcw_macro.h", "nvt_type.h"):
            path = os.path.join(src.mach, name)
            if os.path.isfile(path):
                shutil.copy(path, os.path.join(shim, "mach"))
        os.symlink(src.plat, os.path.join(shim, "plat"))

        csrc = os.path.join(tmp, "harness.c")
        with open(csrc, "w") as f:
            f.write(build_harness(src, enums, recs))
        exe = os.path.join(tmp, "harness")
        res = subprocess.run([cc, "-std=gnu99", "-w", "-O1", "-o", exe, csrc,
                              "-I", shim, "-I", src.drv,
                              '-DFAMILY_PINMUX_H="%s_pinmux.h"' % src.family,
                              "-DNVT_CHIP=%#x" % chip_id],
                             capture_output=True, text=True)
        if res.returncode != 0:
            die("the harness does not compile:\n" + res.stderr[-4000:])
        res = subprocess.run([exe], capture_output=True, text=True)
        if res.returncode != 0:
            die("the harness failed: %s" % res.stderr[-2000:])
        return res.stdout
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# ---------------------------------------------------------------- analysis

Field = collections.namedtuple("Field", "reg mask")


def field_shift(mask):
    return (mask & -mask).bit_length() - 1


class Run:
    """One call: what it ended up writing, and where each write was."""

    def __init__(self, func, opts):
        self.func = func
        self.opts = opts          # option names, declaration order
        self.prefix = None        # another group's option called first
        self.fields = {}          # Field -> final value in place
        self.blocks = {}          # block -> {Field: last value written in it}
        self.gates = {}           # pad -> (0 function / 1 GPIO, rec id, seq)
        self.writes = {}          # Field -> [(seq, value, block, rec id)]
        self.seq = 0
        self.regs = None          # register offset -> final value


def parse_runs(out, enums, recs):
    runs = []
    cur = None
    func_bits = [option_bits(enums, e) for _, e in FUNCS]
    for line in out.splitlines():
        tag, _, rest = line.partition(" ")
        if tag == "U":
            ofs, var = rest.split()
            if "gpio" in var and int(ofs, 16) not in GATE_REGS:
                die("a gate register at %s this does not know" % ofs)
        elif tag == "R":
            parts = [int(x) for x in rest.split()]
            fi = parts[0]
            cur = Run(FUNCS[fi][0],
                      tuple(func_bits[fi][i] for i in sorted(parts[1:])))
        elif tag == "P":
            gi, bi = (int(x) for x in rest.split())
            cur.prefix = func_bits[gi][bi]
        elif tag == "W":
            ofs, mask, val, rid = rest.split()
            if ofs == "?":
                die("a register write the harness cannot place")
            ofs, mask, val, rid = (int(ofs, 16), int(mask, 16), int(val, 16),
                                   int(rid))
            if ofs in GATE_REGS:
                if mask & (mask - 1):
                    die("a multi-bit gate write at %#x" % ofs)
                pad = "%s%d" % (GATE_REGS[ofs][1], field_shift(mask))
                cur.gates[pad] = (1 if val else 0, rid, cur.seq)
            else:
                f = Field(ofs, mask)
                cur.fields[f] = val
                cur.blocks.setdefault(recs[rid].path[-1], {})[f] = val
                cur.writes.setdefault(f, []).append(
                    (cur.seq, val, recs[rid].path[-1], rid))
            cur.seq += 1
        elif tag == "E":
            if int(rest) == 0:
                regs = collections.defaultdict(int)
                for f, v in cur.fields.items():
                    regs[f.reg] = (regs[f.reg] & ~f.mask) | v
                cur.regs = regs
                runs.append(cur)
            cur = None
    return runs


class Claim:
    __slots__ = ("pad", "name", "conds", "ungated")

    def __init__(self, pad, name, conds, ungated):
        self.pad = pad
        self.name = name
        self.conds = conds        # tuple of (Field, value), sorted
        self.ungated = ungated

    def sort_key(self):
        # Most conditions first; then, of two alike, the gated one, whose
        # cleared gate is one more piece of evidence -- CCIR8 data and CCIR8
        # sync both hold under SENSOR2=2, and a handed-over pad is the sync.
        return (pad_number(self.pad), -len(self.conds), self.ungated,
                self.name, self.conds)

    def holds(self, regs, gate):
        return (self.ungated or gate == 0) and all(
            (regs[f.reg] & f.mask) == v for f, v in self.conds)


# A pad the other group's option wrote, in a call that needed one first: what
# that option does to it is that group's run to say, not this one's.
PREFIX = "prefix"


class Model:
    def __init__(self, runs, recs, names, bonded):
        self.runs = runs
        self.recs = recs
        self.names = names
        self.bonded = set(bonded)
        self.order = {f: i for i, (f, _) in enumerate(FUNCS)}
        self.claims = {}          # pad -> [Claim], resolver order
        self.chosen = {}          # (pad, conds, ungated) -> (unnamed?, name)
        self.explained = set()    # (option, pad) the code touches
        self.problems = []
        self.conflicts = 0        # states where two claims hold at once
        self.orphans = 0          # pads cleared and then claimed by nothing

    def own(self, run, pad):
        """The claim the last gate write of `pad` in `run` makes:
        (conds, ungated, rec), or None for a pad it leaves a GPIO."""
        if pad not in run.gates:
            return None
        val, rid, seq = run.gates[pad]
        rec = self.recs[rid]
        if rec.func != run.func:
            return PREFIX
        if val == 1 and run.func not in UNGATED_FUNCS:
            return None
        return tuple(sorted(self.conds_at(run, rec, seq).items())), \
            val == 1, rec

    @staticmethod
    def conds_at(run, rec, seq):
        """The fields a gate write at `seq` depends on, and their values.

        A field counts when a block enclosing the gate write writes it, and
        it takes the value the last such write gave it -- UART2 = 2 comes
        after its pads' gates in the same block and still counts. One
        exception. A field an enclosing block wrote and a block NESTED in it
        then overwrote, both before the gate write, takes the nested value:
        pinmux_select_primary_lcd() resets LCD_TYPE to GPIO at its top, sets
        it per type in a switch, and only then hands over the pads."""
        enclosing = set(rec.path)
        conds = {}
        for f, writes in run.writes.items():
            own = [w for w in writes if w[2] in enclosing]
            if not own:
                continue
            value = own[-1][1]
            before = [w for w in writes if w[0] < seq]
            own_before = [w for w in own if w[0] < seq]
            if (before and before[-1][2] not in enclosing and own_before and
                    before[-1][0] > own_before[-1][0] and
                    not any(w[0] > seq for w in own)):
                value = before[-1][1]
            conds[f] = value
        return conds

    def name_for(self, run, pad, rec):
        """(unnamed?, name): the enclosing options innermost first, then the
        options of the call; failing both, the innermost option's label."""
        for opt in rec.chain + run.opts[::-1]:
            for sub in (run.func, None):
                n = self.names.get((sub, opt, pad))
                if n:
                    return (False, n)
        if rec.chain:
            return (True, option_label(rec.chain[0]))
        if run.func == "lcd":
            return (True, option_label(run.opts[0]))  # the type, not a flag
        return (True, option_label(run.opts[-1]))

    def add(self, pad, name, conds, ungated):
        """Add a claim. Returns True when it is new, False when it was
        there, and the clashing claim when one has the same conditions and
        another name."""
        lst = self.claims.setdefault(pad, [])
        for c in lst:
            if c.conds == conds and c.ungated == ungated:
                return c if c.name != name else False
        lst.append(Claim(pad, name, conds, ungated))
        lst.sort(key=Claim.sort_key)
        return True

    def build(self):
        seen = collections.defaultdict(set)
        for run in self.runs:
            for pad in run.gates:
                own = self.own(run, pad)
                if own is None or own is PREFIX:
                    continue
                conds, ungated, rec = own
                for opt in rec.chain + run.opts:
                    self.explained.add((opt, pad))
                if pad in self.bonded:
                    unnamed, name = self.name_for(run, pad, rec)
                    seen[(pad, conds, ungated)].add(
                        (unnamed, self.order[run.func], name))

        # One field reached from two groups -- SEN_MCLK is set by both
        # PIN_SENSOR_CFG_MCLK and PIN_SENSOR2_CFG_SN_MCLK -- takes the name
        # the group earlier in the vendor's table gives it.
        for key, cands in seen.items():
            named = {n for u, _, n in cands if not u}
            if len(named) > 1:
                self.problems.append("%s is %s under the same writes"
                                     % (key[0], " and ".join(sorted(named))))
            u, _, n = min(cands)
            self.chosen[key] = (u, n)

        # A state nothing names inherits the name of the state it extends:
        # SPI_CFG_CH1_2BITS on its own does everything CH1 does plus SPI_DAT,
        # and C_GPIO17 is still SPI_1_CLK. Only from a strict subset of the
        # same pad's conditions, the largest, so a name never moves sideways.
        for key, (unnamed, _) in list(self.chosen.items()):
            if not unnamed:
                continue
            pad, conds, ungated = key
            best = None
            for other, (u, name) in self.chosen.items():
                if (not u and other[0] == pad and other[2] == ungated and
                        len(other[1]) < len(conds) and
                        set(other[1]) <= set(conds) and
                        (best is None or len(other[1]) > best[0])):
                    best = (len(other[1]), name)
            if best:
                self.chosen[key] = (False, best[1])

        for (pad, conds, ungated), (_, name) in self.chosen.items():
            res = self.add(pad, name, conds, ungated)
            if isinstance(res, Claim):
                self.problems.append("%s is %s and %s under the same writes"
                                     % (pad, res.name, name))

        for _ in range(8):
            if not self.replay():
                break
        else:
            self.problems.append("the replay did not settle")

    def replay(self):
        """Check every state the vendor code produced; add a claim for each
        overlap the code resolves. Returns how many were added."""
        added = 0
        self.conflicts = self.orphans = 0
        ungated_pads = {p for p, lst in self.claims.items()
                        if any(c.ungated for c in lst)}
        for run in self.runs:
            for pad in sorted((set(run.gates) | ungated_pads) & self.bonded):
                gate = run.gates.get(pad, (1,))[0]
                held = [c for c in self.claims.get(pad, ())
                        if c.holds(run.regs, gate)]
                held_names = {c.name for c in held}
                own = self.own(run, pad)
                if own is PREFIX:
                    continue
                want = None
                if own is not None:
                    conds, ungated, _ = own
                    if all((run.regs[f.reg] & f.mask) == v for f, v in conds):
                        want = self.chosen[(pad, conds, ungated)][1]
                    else:
                        self.orphans += 1
                if want is None:
                    if held_names:
                        # Cleared, its own claim moved away, and something
                        # else's conditions happen to hold: the vendor code
                        # says nothing about what that pad is.
                        self.conflicts += 1
                    continue
                if len(held_names) > 1:
                    self.conflicts += 1
                if held[0].name == want:
                    continue
                if want not in held_names:
                    self.problems.append(
                        "%s after %s: the code means %s, the claims say %s"
                        % (pad, list(run.opts), want,
                           sorted(held_names) or "nothing"))
                    continue
                # Two hold; the code meant `want`. Make that the more
                # specific claim: its own conditions and the others'.
                conds = dict(own[0])
                for c in held:
                    for f, _ in c.conds:
                        conds[f] = run.regs[f.reg] & f.mask
                conds = tuple(sorted(conds.items()))
                res = self.add(pad, want, conds, own[1])
                if isinstance(res, Claim):
                    self.problems.append(
                        "%s after %s is both %s and %s"
                        % (pad, list(run.opts), res.name, want))
                elif res:
                    added += 1
                else:
                    self.problems.append(
                        "%s after %s reads as %s, and %s cannot be made to "
                        "win" % (pad, list(run.opts), held[0].name, want))
        return added

    def check_annotations(self, found):
        for opt, pad in found:
            if (opt, pad) in self.explained:
                continue
            # An option the vendor refuses in every combination explains
            # nothing and contradicts nothing.
            if not any(opt in r.opts for r in self.runs):
                continue
            self.problems.append(
                "%s names %s, which the code never touches for it"
                % (opt, pad))


# ---------------------------------------------------------------- output


def pick_selectors(claims):
    """For each (pad, name): the condition its walk row shows as address,
    mask and value, or None for the gate bit.

    It is information only -- get() and set() read the claims -- but a
    consumer that sees two rows of one pad with the same selector cannot
    tell them apart, so no pad may show one twice. Candidates are the
    conditions every claim of the name shares, least shared with the pad's
    other names first; then the rest of its conditions; then the gate bit.
    Picked as a bipartite matching: greedy gets stuck on the HSI pads."""
    out = {}
    for pad, lst in claims.items():
        by_name = collections.OrderedDict()
        for c in sorted(lst, key=lambda c: (len(c.conds), c.name)):
            by_name.setdefault(c.name, []).append(c)

        def shared(cond, name):
            return sum(1 for n, cs in by_name.items()
                       if n != name and any(cond in c.conds for c in cs))

        cands = {}
        for name, cs in by_name.items():
            common = set(cs[0].conds)
            for c in cs[1:]:
                common &= set(c.conds)
            rest = {f for c in cs for f in c.conds} - common
            order = sorted(common, key=lambda f: (shared(f, name), f))
            order += sorted(rest, key=lambda f: (shared(f, name), f))
            cands[name] = [(f.reg, f.mask, v) for f, v in order] + [None]

        match = {}

        def augment(name, tried):
            for sel in cands[name]:
                if sel in tried:
                    continue
                tried.add(sel)
                if sel not in match or augment(match[sel], tried):
                    match[sel] = name
                    return True
            return False

        for name in by_name:
            if not augment(name, set()):
                die("%s: no selector left for %s" % (pad, name))
        for sel, name in match.items():
            out[(pad, name)] = sel
    return out


def packed(f, v):
    """reg, shift, width, value: what novatek_cond_t holds."""
    shift = field_shift(f.mask)
    width = bin(f.mask).count("1")
    if f.mask != ((1 << width) - 1) << shift:
        die("a field that is not contiguous: %#x" % f.mask)
    if width > 8 or f.reg > 0xFF:
        die("a field too wide for the table: %#x at %#x" % (f.mask, f.reg))
    return f.reg, shift, width, v >> shift


def render_soc(chip, model, bonded):
    """One SoC's four tables and its novatek_soc_t, `chip`_padmux."""
    pads = sorted(model.claims, key=pad_number)
    conds = []
    index = {}
    rows = []
    for pad in pads:
        for c in model.claims[pad]:
            if c.conds not in index:
                index[c.conds] = len(conds)
                conds.extend(c.conds)
            rows.append((c, index[c.conds]))
    if len(conds) > 0xFFFF:
        die("too many conditions for a uint16_t index")
    sel = pick_selectors(model.claims)

    o = []
    o.append("/* %s: %d vendor states replayed; in %d of them two claims hold"
             % (chip, len(model.runs), model.conflicts))
    o.append(" * at once and the table carries the one the vendor code meant,")
    o.append(" * and %d leave a pad cleared with its field moved elsewhere,"
             % model.orphans)
    o.append(" * which reads as unnamed. */")
    o.append("")
    o.append("/* Every pad the package bonds out, by Linux GPIO number. */")
    o.append("static const novatek_pad_t %s_pads[] = {" % chip)
    for p in bonded:
        o.append("    {%d, PMX_%s}," % (pad_number(p), p))
    o.append("};")
    o.append("")
    o.append("/* TOP register offset, field shift, width, value. */")
    o.append("static const novatek_cond_t %s_conds[] = {" % chip)
    for f, v in conds:
        o.append("    {0x%02X, %d, %d, %d}," % packed(f, v))
    o.append("};")
    o.append("")
    o.append("/* Per pad, most conditions first: the first claim that holds")
    o.append(" * is what the pad carries. {pad, flags, name, first")
    o.append(" * condition, how many}. */")
    o.append("static const novatek_claim_t %s_claims[] = {" % chip)
    for c, idx in rows:
        o.append("    {%d, %s, PMX_%s, %d, %d}, /* %s */" % (
            pad_number(c.pad), "NVT_UNGATED" if c.ungated else "0",
            c.name, idx, len(c.conds), c.pad))
    o.append("};")
    o.append("")
    o.append("/* The field each function's walk row shows: {pad, name, reg,")
    o.append(" * shift, width, value}, reg NVT_GATE for the pad's gate bit. */")
    o.append("static const novatek_sel_t %s_sels[] = {" % chip)
    nsels = 0
    for pad in pads:
        done = set()
        for c in model.claims[pad]:
            if c.name in done:
                continue
            done.add(c.name)
            s = sel[(pad, c.name)]
            if s is None:
                o.append("    {%d, PMX_%s, NVT_GATE, 0, 0, 0}," % (
                    pad_number(pad), c.name))
            else:
                f = Field(s[0], s[1])
                o.append("    {%d, PMX_%s, 0x%02X, %d, %d, %d}," % (
                    (pad_number(pad), c.name) + packed(f, s[2])))
            nsels += 1
    o.append("};")
    o.append("")
    o.append("static const novatek_soc_t %s_padmux = {" % chip)
    o.append("    %s_pads, %d, %s_claims, %d, %s_conds,"
             % (chip, len(bonded), chip, len(rows), chip))
    o.append("    %s_sels, %d," % (chip, nsels))
    o.append("};")
    return o


def render(socs, regen):
    o = []
    o.append("""/* Generated by tools/gen_novatek_padmux.py -- do not edit.
 *
 * What the vendor's pinmux_config_*() and pinmux_select_primary_lcd() do to
 * each pad, found by running them: see the generator for how, and
 * src/hal/novatek_padmux.c for how the tables are read. One table per die;
 * NA51055 and NA51084 come from one driver run as each.
 *
 * Regenerate with:
 *   %s
 */

#ifndef HAL_NOVATEK_PADMUX_H
#define HAL_NOVATEK_PADMUX_H

#include "hal/novatek_padmux_types.h"
""" % regen)
    for chip, model, bonded in socs:
        o.extend(render_soc(chip, model, bonded))
        o.append("")
    o.append("#endif /* HAL_NOVATEK_PADMUX_H */")
    return "\n".join(o) + "\n"


REGEN = "tools/gen_novatek_padmux.py --sdk <na51089 SDK root>"


def generate(sdk):
    """Every family the SDK carries, each run as every die it serves.
    Returns the header and {chip: Model}."""
    socs = []
    models = {}
    problems = []
    for family, chips, uses_csv in FAMILIES:
        src = Sources(sdk, family)
        enums = parse_enums(read(src.top_h))
        bonded = bonded_pads(read(src.gpio_h))
        csv = read(src.csv) if uses_csv and src.csv else ""
        names, found = annotations(enums, csv, family)
        explained = set()
        family_models = []
        for chip, chip_id in chips:
            recs = []
            out = run_harness(src, enums, recs, chip_id)
            runs = parse_runs(out, enums, recs)
            model = Model(runs, recs, names, bonded)
            model.build()
            problems.extend("%s: %s" % (chip, p) for p in model.problems)
            explained |= model.explained
            family_models.append(model)
            socs.append((chip, model, bonded))
            models[chip] = model
        # An annotation one die of the family explains is not a source error:
        # NA51084 refuses UART2's 3rd location, NA51055 does not.
        for model in family_models:
            model.explained = explained
        family_models[0].problems = []
        family_models[0].check_annotations(found)
        problems.extend("%s: %s" % (family, p)
                        for p in family_models[0].problems)
    if problems:
        die("the sources disagree:\n  " + "\n  ".join(problems[:40]))
    return render(socs, REGEN), models


# ---------------------------------------------------------------- selftest

# A fake SDK in the shapes that matter: a location field with two values,
# a modifier that only counts inside its base option's block, an ungated data
# bus and a gated claim overlapping it, a field written to zero on purpose,
# a field moved away by a later option, a GPIO write that is a release, a
# claim the code resolves between two that hold, a brace inside a string,
# and a csv typo.
FIXTURE = {
    "BSP/linux-kernel/arch/arm/mach-nvt-ivot/include/mach/rcw_macro.h": """
enum { E_OK = 0, E_PAR = -1, E_OBJ = -2 };
""",
    "BSP/linux-kernel/arch/arm/mach-nvt-ivot/include/mach/nvt_type.h": "",
    "BSP/linux-kernel/arch/arm/plat-novatek/include/plat-na51089/top_reg.h":
    """
#include <mach/rcw_macro.h>
#define TOP_REG2_OFS 0x08
union TOP_REG2 { uint32_t reg; struct {
    unsigned int LCD_TYPE:4; unsigned int r0:28; } bit; };
#define TOP_REG3_OFS 0x0C
union TOP_REG3 { uint32_t reg; struct {
    unsigned int SENSOR:3; unsigned int r0:3;
    unsigned int SEN_CCIR_VSHS:1; unsigned int r1:1;
    unsigned int SEN_MCLK:2; unsigned int r2:22; } bit; };
#define TOP_REG5_OFS 0x14
union TOP_REG5 { uint32_t reg; struct {
    unsigned int r0:2; unsigned int ETH_MDIO:2;
    unsigned int r1:2; unsigned int I2C3:2; unsigned int r2:22;
    unsigned int ETH:2; } bit; };
#define TOP_REG9_OFS 0x24
union TOP_REG9 { uint32_t reg; struct {
    unsigned int r0:2; unsigned int UART2:2; unsigned int r1:4;
    unsigned int UART2_CTSRTS:2; unsigned int r2:22; } bit; };
#define TOP_REGCGPIO0_OFS 0xA0
union TOP_REGCGPIO0 { uint32_t reg; struct {
    unsigned int CGPIO_0:1; unsigned int CGPIO_1:1; unsigned int r:9;
    unsigned int CGPIO_11:1;
    unsigned int CGPIO_12:1; unsigned int CGPIO_13:1; unsigned int r2:18;
    } bit; };
#define TOP_REGPGPIO0_OFS 0xA8
union TOP_REGPGPIO0 { uint32_t reg; struct {
    unsigned int PGPIO_0:1; unsigned int PGPIO_1:1; unsigned int PGPIO_2:1;
    unsigned int r:29; } bit; };
#define TOP_REGHGPIO0_OFS 0xD8
union TOP_REGHGPIO0 { uint32_t reg; struct {
    unsigned int HSIGPIO_0:1; unsigned int HSIGPIO_1:1; unsigned int r:30;
    } bit; };
""",
    "BSP/linux-kernel/arch/arm/plat-novatek/include/plat-na51089/top.h": """
typedef enum {
    PIN_I2C_CFG_NONE,
    PIN_I2C_CFG_CH3 = 0x40, ///< @P_GPIO1[I2C3_1_SCL]|P_GPIO0[I2C3_1_SDA]
    PIN_I2C_CFG_CH3_2ND_PINMUX = 0x80, ///< @MC11[I2C3_2_SCL]|MC12[I2C3_2_SDA]
} PIN_I2C_CFG;
typedef enum {
    PIN_UART_CFG_NONE,
    PIN_UART_CFG_CH2 = 0x04, ///< @P_GPIO2[Uart2_1_TX]
    PIN_UART_CFG_CH2_CTSRTS = 0x08, ///< flow control
    PIN_UART_CFG_CH2_2ND = 0x10, ///< @MC11[Uart2_2_TX]. RTS &&MC13[Uart2_2_RTS]
} PIN_UART_CFG;
typedef enum {
    PIN_SENSOR_CFG_NONE,
    PIN_SENSOR_CFG_12BITS = 0x04, ///< parallel on HSI_GPI[0..1]
    PIN_SENSOR_CFG_CCIR8BITS = 0x08, ///< no annotation
    PIN_SENSOR_CFG_MCLK = 0x10, ///< mclk on C0
    PIN_SENSOR_CFG_MCLK_2ND = 0x20, ///< mclk on HSI1
    PIN_SENSOR_CFG_VDHD = 0x40, ///< sync on HSI0, inside the 12-bit mode
} PIN_SENSOR_CFG;
typedef enum {
    PIN_SPI_CFG_NONE,
    PIN_SPI_CFG_CH1 = 1,
    PIN_SPI_CFG_DI_ONLY = 2,
} PIN_SPI_CFG;
typedef enum {
    PIN_ETH_CFG_NONE,
    PIN_ETH_CFG_RMII = 1,
    PIN_ETH_CFG_INTERANL = 2,
    PIN_ETH_CFG_MDIO = 4,
} PINMUX_ETH_CFG;
typedef enum {
    PIN_MISC_CFG_NONE,
    PIN_MISC_CFG_A = 1,
    PIN_MISC_CFG_B = 2,
} PINMUX_MISC_CFG;
""",
    "BSP/linux-kernel/arch/arm/plat-novatek/include/plat-na51089/"
    "nvt-gpio.h": """
#define C_GPIO_NUM 14
#define P_GPIO_NUM 3
#define S_GPIO_NUM 0
#define L_GPIO_NUM 0
#define D_GPIO_NUM 0
#define H_GPIO_NUM 2
#define A_GPIO_NUM 0
#define DSI_GPIO_NUM 0
""",
    "BSP/linux-kernel/drivers/pinctrl/novatek/na51089/na51089_pinmux.h": """
#include <plat/top_reg.h>
#include <plat/top.h>
enum { GPIO_ID_EMUM_FUNC, GPIO_ID_EMUM_GPIO };
""",
    "BSP/linux-kernel/drivers/pinctrl/novatek/na51089/"
    "na51089_pinmux_host.c": r"""
#include "na51089_pinmux.h"

static int pinmux_config_i2c(uint32_t config);
static int pinmux_config_uart(uint32_t config);
static int pinmux_config_sensor(uint32_t config);
static int pinmux_config_spi(uint32_t config);
static int pinmux_config_eth(uint32_t config);
static int pinmux_config_misc(uint32_t config);
static int sticky = 0;

union TOP_REG2 top_reg2;
union TOP_REG3 top_reg3;
union TOP_REG5 top_reg5;
union TOP_REG9 top_reg9;
union TOP_REGCGPIO0 top_reg_cgpio0;
union TOP_REGPGPIO0 top_reg_pgpio0;
union TOP_REGHGPIO0 top_reg_hgpio0;

struct nvt_pinctrl_info info_get_id[1];

static int pinmux_config_i2c(uint32_t config)
{
    if (config & PIN_I2C_CFG_CH3) {
        top_reg_pgpio0.bit.PGPIO_1 = GPIO_ID_EMUM_FUNC;
        top_reg_pgpio0.bit.PGPIO_0 = GPIO_ID_EMUM_FUNC;
        top_reg5.bit.I2C3 = 1;
    }
    if (config & PIN_I2C_CFG_CH3_2ND_PINMUX) {
        if (top_reg9.bit.UART2 == 2) {
            pr_err("conflict with UART2_2 }\n");
            return E_OBJ;
        }
        top_reg_cgpio0.bit.CGPIO_11 = GPIO_ID_EMUM_FUNC;
        top_reg_cgpio0.bit.CGPIO_12 = GPIO_ID_EMUM_FUNC;
        top_reg5.bit.I2C3 = 2;
    }
    return E_OK;
}

static int pinmux_config_uart(uint32_t config)
{
    if (config & PIN_UART_CFG_CH2) {
        if (config & PIN_UART_CFG_CH2_2ND) {
            if (config & PIN_UART_CFG_CH2_CTSRTS) {
                top_reg_cgpio0.bit.CGPIO_13 = GPIO_ID_EMUM_FUNC;
                top_reg9.bit.UART2_CTSRTS = 1;
            }
            top_reg_cgpio0.bit.CGPIO_11 = GPIO_ID_EMUM_FUNC;
            top_reg9.bit.UART2 = 2;
        } else {
            top_reg_pgpio0.bit.PGPIO_2 = GPIO_ID_EMUM_FUNC;
            top_reg9.bit.UART2 = 1;
        }
    }
    return E_OK;
}

static int pinmux_config_sensor(uint32_t config)
{
    uint32_t tmp = config & (PIN_SENSOR_CFG_12BITS | PIN_SENSOR_CFG_CCIR8BITS);

    sticky++;
    switch (tmp) {
    case PIN_SENSOR_CFG_12BITS:
        top_reg3.bit.SENSOR = 1;
        top_reg_hgpio0.bit.HSIGPIO_0 = GPIO_ID_EMUM_GPIO;
        top_reg_hgpio0.bit.HSIGPIO_1 = GPIO_ID_EMUM_GPIO;
        if (config & PIN_SENSOR_CFG_VDHD) {
            top_reg_hgpio0.bit.HSIGPIO_0 = GPIO_ID_EMUM_FUNC;
        }
        break;
    case PIN_SENSOR_CFG_CCIR8BITS:
        top_reg3.bit.SENSOR = 3;
        top_reg3.bit.SEN_CCIR_VSHS = 0;
        top_reg_hgpio0.bit.HSIGPIO_0 = GPIO_ID_EMUM_FUNC;
        break;
    default:
        return E_PAR;
    }
    if (config & PIN_SENSOR_CFG_MCLK) {
        top_reg_cgpio0.bit.CGPIO_0 = GPIO_ID_EMUM_FUNC;
        top_reg3.bit.SEN_MCLK = 1;
    }
    if (config & PIN_SENSOR_CFG_MCLK_2ND) {
        top_reg_hgpio0.bit.HSIGPIO_1 = GPIO_ID_EMUM_FUNC;
        top_reg3.bit.SEN_MCLK = 2;
    }
    return sticky > 1000000 ? E_PAR : E_OK;
}

static int pinmux_config_spi(uint32_t config)
{
    if (config & PIN_SPI_CFG_DI_ONLY) {
        top_reg_pgpio0.bit.PGPIO_2 = GPIO_ID_EMUM_GPIO;
    } else if (config & PIN_SPI_CFG_CH1) {
        top_reg_pgpio0.bit.PGPIO_2 = GPIO_ID_EMUM_FUNC;
    }
    return E_OK;
}

static int pinmux_config_eth(uint32_t config)
{
    if (config & PIN_ETH_CFG_INTERANL) {
        if (config & PIN_ETH_CFG_MDIO) {
            top_reg_pgpio0.bit.PGPIO_1 = GPIO_ID_EMUM_FUNC;
            top_reg5.bit.ETH_MDIO = 1;
        }
    } else if (config & PIN_ETH_CFG_RMII) {
        if (config & PIN_ETH_CFG_RMII) {
            top_reg_pgpio0.bit.PGPIO_1 = GPIO_ID_EMUM_FUNC;
            top_reg5.bit.ETH = 1;
        }
        if (config & PIN_ETH_CFG_MDIO) {
            top_reg_pgpio0.bit.PGPIO_0 = GPIO_ID_EMUM_FUNC;
            top_reg5.bit.ETH_MDIO = 1;
        }
    }
    return E_OK;
}

static int pinmux_config_misc(uint32_t config)
{
    top_reg2.bit.LCD_TYPE = 0;
    switch (config & (PIN_MISC_CFG_A | PIN_MISC_CFG_B)) {
    case PIN_MISC_CFG_A:
        top_reg2.bit.LCD_TYPE = 1;
        break;
    case PIN_MISC_CFG_B:
        top_reg2.bit.LCD_TYPE = 2;
        break;
    default:
        break;
    }
    if ((config >= PIN_MISC_CFG_A) && (config <= PIN_MISC_CFG_B)) {
        top_reg_cgpio0.bit.CGPIO_1 = GPIO_ID_EMUM_FUNC;
    }
    return E_OK;
}
""",
    "build/nvt-tools/nvt_pinctrl_tool/top.csv": """\
PIN_SPI_CFG_CH1,1,P_GPIO2[spi_clk(BS)],
PIN_I2C_CFG_CH3,64,P_GPIO29[I2C3_1_SCL],
""",
}

SELFTEST_FUNCS = [("i2c", "PIN_I2C_CFG"), ("uart", "PIN_UART_CFG"),
                  ("sensor", "PIN_SENSOR_CFG"), ("spi", "PIN_SPI_CFG"),
                  ("eth", "PINMUX_ETH_CFG"), ("misc", "PINMUX_MISC_CFG")]


def selftest():
    global FUNCS, FAMILIES
    tmp = tempfile.mkdtemp(prefix="nvt_padmux_selftest_")
    saved = FUNCS, dict(ANNOTATION_ERRATA), FAMILIES
    failures = []

    def check(cond, what):
        if not cond:
            failures.append(what)

    try:
        for rel, text in FIXTURE.items():
            path = os.path.join(tmp, rel)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w") as f:
                f.write(text)
        FUNCS = SELFTEST_FUNCS
        FAMILIES = [("na51089", [("NA51089", 0x7021)], True)]

        # The csv's P_GPIO29 is a typo. Without an erratum the sources
        # disagree and the generator has to say so rather than guess.
        try:
            generate(tmp)
            check(False, "a csv pad the code never touches was accepted")
        except SystemExit as e:
            check("PIN_I2C_CFG_CH3 names P_GPIO29" in str(e),
                  "refusal names the pad: %s" % e)

        ANNOTATION_ERRATA[("na51089", "PIN_I2C_CFG_CH3", "P_GPIO29")] = (
            "PIN_I2C_CFG_CH3", "P_GPIO1")
        text, models = generate(tmp)
        model = models["NA51089"]
        claims = model.claims

        def names(pad):
            return {c.name for c in claims.get(pad, [])}

        def read_as(pad, writes, gate):
            regs = collections.defaultdict(int)
            for (reg, mask), v in writes.items():
                regs[reg] = (regs[reg] & ~mask) | v
            for c in claims.get(pad, ()):
                if c.holds(regs, gate):
                    return c.name
            return None if gate else "?"

        # Names: top.h, case folded; the csv, "(BS)" dropped; else a label.
        check(names("C_GPIO12") == {"I2C3_2_SDA"}, "C12 %s"
              % names("C_GPIO12"))
        check(names("C_GPIO11") == {"I2C3_2_SCL", "UART2_2_TX"},
              "C11 %s" % names("C_GPIO11"))
        # A modifier's pad, named from the && list of its enclosing option,
        # and conditioned on both blocks' fields.
        rts = claims.get("C_GPIO13", [])
        check([c.name for c in rts] == ["UART2_2_RTS"], "C13 %s"
              % [c.name for c in rts])
        check(rts and len(rts[0].conds) == 2, "C13 needs UART2 and CTSRTS")
        check(names("P_GPIO2") == {"UART2_1_TX", "SPI_CLK"}, "P2 %s"
              % names("P_GPIO2"))
        check("SENSOR_12BITS" in names("H_GPIO0"), "H0 %s" % names("H_GPIO0"))
        # MCLK depends on SEN_MCLK alone, not on the sensor mode the same
        # call selected in another block.
        mclk = claims.get("C_GPIO0", [])
        check([len(c.conds) for c in mclk] == [1], "MCLK conds %s"
              % [c.conds for c in mclk])

        u = [c for c in claims["H_GPIO0"] if c.name == "SENSOR_12BITS"]
        check(u and u[0].ungated, "12BITS data is ungated")
        cc = [c for c in claims["H_GPIO0"] if c.name == "SENSOR_CCIR8BITS"]
        check(cc and any(f.mask == 0x40 and v == 0 for f, v in cc[0].conds),
              "SEN_CCIR_VSHS=0 is kept as a condition")
        check(all(not c.ungated for c in claims["P_GPIO2"]),
              "a release is not a claim")

        I2C3, UART2, SENSOR, MCLK = (0x14, 0xC0), (0x24, 0x0C), (0x0C, 0x07), \
            (0x0C, 0x300)
        ETH, MDIO = (0x14, 0xC0000000), (0x14, 0x0C)
        check(read_as("C_GPIO11", {I2C3: 0x80}, 0) == "I2C3_2_SCL",
              "C11 under I2C3=2")
        check(read_as("C_GPIO11", {UART2: 0x08}, 0) == "UART2_2_TX",
              "C11 under UART2=2")
        check(read_as("C_GPIO11", {I2C3: 0x80}, 1) is None, "the gate wins")
        check(read_as("C_GPIO11", {}, 0) == "?", "cleared, claimed by none")
        check(read_as("H_GPIO0", {SENSOR: 1}, 1) == "SENSOR_12BITS",
              "ungated holds through the gate")
        check(read_as("H_GPIO1", {SENSOR: 1, MCLK: 0x200}, 0) ==
              "SENSOR_MCLK_2ND", "H1 %s"
              % read_as("H_GPIO1", {SENSOR: 1, MCLK: 0x200}, 0))
        # MCLK then MCLK_2ND: C_GPIO0 is cleared and its field went away.
        check(read_as("C_GPIO0", {SENSOR: 1, MCLK: 0x200}, 0) == "?",
              "orphan reads as nothing")
        check(model.orphans > 0, "orphans counted")
        # P_GPIO1 is RMII data and internal-PHY MDIO; with RMII and MDIO
        # both set the code meant RMII.
        both = {ETH: 0x40000000, MDIO: 0x4}
        check(read_as("P_GPIO1", both, 0) == "ETH_RMII", "P1 RMII+MDIO %s"
              % read_as("P_GPIO1", both, 0))
        check(read_as("P_GPIO1", {MDIO: 0x4}, 0) == "ETH_MDIO", "P1 MDIO")

        # Gated before ungated, conditions alike: a handed-over HSI0 under
        # the 12-bit mode is the sync, a gated one is still the data.
        check(read_as("H_GPIO0", {SENSOR: 1}, 0) == "SENSOR_VDHD",
              "H0 gate clear %s" % read_as("H_GPIO0", {SENSOR: 1}, 0))
        check(read_as("H_GPIO0", {SENSOR: 1}, 1) == "SENSOR_12BITS",
              "H0 gate set")
        # Reset at the top, set in a switch, then the pad: the switch's
        # value is the condition, and a range test names no option.
        LCD = (0x08, 0x0F)
        check(names("C_GPIO1") == {"MISC_A", "MISC_B"}, "C1 %s"
              % names("C_GPIO1"))
        check(read_as("C_GPIO1", {LCD: 2}, 0) == "MISC_B", "C1 under 2")
        check(read_as("C_GPIO1", {LCD: 0}, 0) == "?", "C1 under 0")

        check("PMX_C_GPIO13" in text and "NA51089_claims" in text, "render")
    finally:
        FUNCS, FAMILIES = saved[0], saved[2]
        ANNOTATION_ERRATA.clear()
        ANNOTATION_ERRATA.update(saved[1])
        shutil.rmtree(tmp, ignore_errors=True)

    for f in failures:
        print("selftest FAIL: %s" % f, file=sys.stderr)
    if failures:
        return 1
    print("selftest: ok")
    return 0


def main():
    ap = argparse.ArgumentParser(
        description=__doc__.split("\n\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sdk", help="NA51089 SDK root (has BSP/linux-kernel)")
    ap.add_argument("--verify", metavar="HEADER",
                    help="regenerate and diff against HEADER instead")
    ap.add_argument("--selftest", action="store_true",
                    help="run against a built-in fixture SDK; needs a C "
                         "compiler and nothing else")
    args = ap.parse_args()

    if args.selftest:
        return selftest()
    if not args.sdk:
        ap.error("--sdk is required")

    text, _ = generate(args.sdk)
    if args.verify:
        have = read(args.verify)
        if have != text:
            sys.stdout.writelines(difflib.unified_diff(
                have.splitlines(True), text.splitlines(True), args.verify,
                "regenerated"))
            return 1
        print("%s: up to date" % args.verify)
        return 0
    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
