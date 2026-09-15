#!/usr/bin/env python3
"""Turn Ingenic's GPIO spec into src/hal/ingenic_padmux.h.

Ingenic muxes per pin, but the selector is not a field anywhere. Four
registers per port -- INT, MSK, PAT1, PAT0 -- carry one bit each at the pin's
own position, and the nibble they spell is what soc/gpio.h calls
enum gpio_function: 0..3 are the four device functions, 4 and 5 are GPIO
driven low and high, 6 is GPIO input. So no single register write selects a
function, which is why the pad table is only the four names and the backend in
src/hal/ingenic_padmux.c does the rest.

Those names come from the "GPIO Port A/B/C summary" tables of the vendor's
GPIO spec, which give FUNCTION0..FUNCTION3 per pad verbatim -- a complete
SoC-wide table, unlike the board file in the kernel, which only names the
peripherals that board happens to wire up.

Usage:
    gen_ingenic_padmux.py --spec T31_H.3_gpio_spec.pdf --soc T31
    gen_ingenic_padmux.py --spec extracted.txt --soc T31 --verify <header>

A .pdf is run through `pdftotext -layout`; anything else is read as that
command's output already, so a maintainer without poppler can still
regenerate from a text dump.

Names are normalised to the spelling the rest of the API uses: uppercased,
the direction annotation dropped, and the trailing _i/_o dropped so
uart1_rxd_i(i-1) is UART1_RXD and pwm0_o(o) is PWM0. "(i-0)" is a pad with
nothing on that function and becomes "reserved", which the lookups skip.
"""

import argparse
import hashlib
import os
import re
import subprocess
import sys

PORTS = "ABCD"
PINS_PER_PORT = 32

# tag, ports, and where the pad names come from. Two kinds of source, because
# only one of these parts has a GPIO spec:
#
#   "spec"     the GPIO spec's "GPIO Port X summary" tables -- FUNCTION0..3 per
#             pad, the whole SoC, named per wire (uart1_rxd_i, pwm0_o).
#   "platform" the vendor kernel's arch/mips/xburst/soc-<x>/include/mach/
#             platform.h -- {name, port, func, pins} claims, named per DEVICE
#             (uart1, dvp-pa-12bit) and only for what the board file wires up.
#
# The second is weaker and it is what exists for T21 and T23. It is still worth
# having: measured against the live registers on a T21 it names 80 of 97 pads
# and on a T23 26 of 67, where the alternative is answering nothing at all.
SOCS = {
    "T31": ("T31", 3, "spec"),
    "T21": ("T21", 6, "platform"),
    "T23": ("T23", 3, "platform"),
}


class Refusal(Exception):
    pass


def spec_text(path):
    if path.lower().endswith(".pdf"):
        try:
            out = subprocess.run(["pdftotext", "-layout", path, "-"],
                                 check=True, capture_output=True)
        except FileNotFoundError:
            raise Refusal("pdftotext is not installed; extract the spec "
                          "yourself and pass the .txt")
        except subprocess.CalledProcessError as exc:
            raise Refusal("pdftotext failed: %s" % exc)
        return out.stdout.decode("utf-8", "replace")
    with open(path, encoding="utf-8", errors="replace") as handle:
        return handle.read()


def sha256(path):
    with open(path, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()[:16]


# "uart1_rxd_i(i-1)", "(i-0)", "msc0_d0(io-1)". The name may be empty, which
# is how the spec writes a function this pad does not have.
FUNC_RE = re.compile(r"([A-Za-z][A-Za-z0-9_]*)?\(([a-z]+)-?\d?\)")
ROW_RE = re.compile(r"^\s*P([A-D])(\d\d)\b(.*)$")
# "Table 1-3 GPIO Port B summary". Table 1-1 is an ILLUSTRATION of the format,
# with three made-up PA00..PA02 rows in it, and taking those for data is the
# one way this parser can be quietly wrong.
SECTION_RE = re.compile(r"GPIO\s+Port\s+([A-D])\s+summary", re.I)


def normalise(name):
    if not name:
        return "reserved"
    name = re.sub(r"_[io]$", "", name)
    return name.upper().replace("-", "_")


FUNCS = {"GPIO_FUNC_0": 0, "GPIO_FUNC_1": 1, "GPIO_FUNC_2": 2, "GPIO_FUNC_3": 3}

# What the board file got wrong, collected so the generated header can say so
# rather than the tool quietly deciding.
CONFLICTS = []
OVERFLOWS = []

# The macro that wraps each claim, and the claim. The macro identifier is the
# name used here rather than the .name inside it, because .name is not unique:
# T21 spells GMAC_PORTB, GMAC_INTERNAL_PHY_PORTB and
# GMAC_INTERNAL_PHY_PORTB_NO_ACTLED all as "gmac_pb", and those are different
# routings on different function codes of the same pads. A pad offering one
# device twice through one name is a pad nothing can mux deterministically.
# A claim, and the #define it sits in. The macro identifier is the name used
# here rather than the .name inside the claim, because .name is not unique:
# T21 spells GMAC_PORTB, GMAC_INTERNAL_PHY_PORTB and
# GMAC_INTERNAL_PHY_PORTB_NO_ACTLED all as "gmac_pb", and those are different
# routings on different function codes of the SAME pads. A pad offering one
# device twice under one name is a pad nothing can mux deterministically.
#
# One macro can hold several claims -- a device that needs pins on two ports,
# say -- and they all belong to that macro.
MACRO_RE = re.compile(r'#\s*define\s+(\w+)')
CLAIM_RE = re.compile(
    r'\{\s*\.name\s*=\s*"[^"]+"\s*,\s*\.port\s*=\s*GPIO_PORT_([A-F])\s*,'
    r'\s*\.func\s*=\s*(\w+)\s*,\s*\.pins\s*=\s*([^,}]+)')


def merge_names(have, want, where, func):
    """Two claims on one pad and one function code.

    The board file spells a device once per pin GROUP it can use, so the same
    pad and function can arrive as ssi0-pb-0 and ssi0-pb-2-15. Which group the
    board wired is not a property of the pad; the device is. Keep what they
    agree on, cut back to a separator so half a word is never the answer, and
    refuse only when they agree on nothing -- that would be two different
    peripherals claiming one function code, which the silicon cannot do.
    """
    if have == "reserved" or have == want:
        return want

    keep = 0
    for i, (a, b) in enumerate(zip(have, want)):
        if a != b:
            break
        if a == "_":
            keep = i
    else:
        keep = min(len(have), len(want))
    stem = have[:keep].rstrip("_")
    if not stem:
        # Two different peripherals claiming one pad and one function code.
        # The silicon cannot do both, so the board file is wrong about one of
        # them and there is no way to tell which. Lose the slot rather than
        # pick, and say so.
        CONFLICTS.append("%s function %d: %s vs %s" % (where, func, have, want))
        return "reserved"
    return stem


def parse_platform(text, soc):
    """The board file's device claims, turned into per-pad function names.

    Each claim is a device, a port, one of the four function codes and a
    bitmask of the pins it lands on -- so a claim fills funcs[func] for every
    pin in the mask. What it cannot say is which WIRE of the device a pad
    carries: a 12-bit DVP bus is twelve pads all called dvp-pa-12bit.

    Two things it gets wrong if taken literally. A claim whose func is a GPIO
    state rather than a device function (GPIO_OUTPUT0 and friends) is a board
    bringing a pin up in a known state, not a mux, and is skipped. And a claim
    on a port the SoC does not have is a copy-paste leftover from a bigger
    part -- T23's board file claims PORT_D and PORT_F, and T23 has three
    ports. The port count comes from the SoC's own enum, not from the claims.
    """
    _, nports, _ = SOCS[soc]
    pads = {}
    del CONFLICTS[:]
    del OVERFLOWS[:]

    # walk the file once, remembering which macro we are inside
    events = [(m.start(), "macro", m.group(1)) for m in MACRO_RE.finditer(text)]
    events += [(m.start(), "claim", m.groups()) for m in CLAIM_RE.finditer(text)]
    events.sort()

    name = None
    for _, kind, payload in events:
        if kind == "macro":
            name = payload
            continue
        if name is None:
            raise Refusal("a devio claim before any #define")
        port, func, pins = payload
        if func not in FUNCS:
            continue
        if ord(port) - ord("A") >= nports:
            continue
        try:
            mask = int(eval(pins.strip().rstrip(","),  # noqa: S307
                            {"__builtins__": {}}, {}))
        except Exception as exc:
            raise Refusal("%s: cannot read pins %r: %s" % (name, pins, exc))
        if mask >> PINS_PER_PORT:
            # The board file's own arithmetic ran off the end of the port --
            # T23 has ssi0-pb-3-29 as 0xf << 29 on a 32-pin port. Take the
            # pins that exist and record the rest.
            OVERFLOWS.append("%s: pins %#x reach past pin %d"
                             % (name, mask, PINS_PER_PORT - 1))
            mask &= (1 << PINS_PER_PORT) - 1

        for pin in range(PINS_PER_PORT):
            if not (mask >> pin & 1):
                continue
            pad = (ord(port) - ord("A")) * PINS_PER_PORT + pin
            slot = pads.setdefault(pad, ["P%s%02d" % (port, pin),
                                         ["reserved"] * 4])
            have = slot[1][FUNCS[func]]
            slot[1][FUNCS[func]] = merge_names(have, normalise(name),
                                               "P%s%02d" % (port, pin),
                                               FUNCS[func])

    if not pads:
        raise Refusal("found no devio claims -- is this a platform.h?")
    return {pad: (v[0], v[1]) for pad, v in pads.items()}


def parse(text, soc):
    _, nports, _ = SOCS[soc]
    pads = {}

    # full-width parentheses appear in a few rows of the T31 spec
    text = text.replace("（", "(").replace("）", ")")

    section = None
    for line in text.splitlines():
        head = SECTION_RE.search(line)
        if head:
            section = None if "illustration" in line.lower() else head.group(1)
            continue
        if re.match(r"^\s*Note\s*:", line):
            section = None
            continue
        if section is None:
            continue

        m = ROW_RE.match(line)
        if not m:
            continue
        port, pin, rest = m.group(1), int(m.group(2)), m.group(3)
        if port != section:
            raise Refusal("P%s%02d appears under the Port %s table"
                          % (port, pin, section))
        if ord(port) - ord("A") >= nports or pin >= PINS_PER_PORT:
            continue

        funcs = [normalise(f.group(1)) for f in FUNC_RE.finditer(rest)]
        if not funcs:
            continue  # a pad the package does not bring out: no row of data
        if len(funcs) != 4:
            raise Refusal("P%s%02d has %d functions, not 4: %s"
                          % (port, pin, len(funcs), rest.strip()))

        named = [f for f in funcs if f != "reserved"]
        if len(set(named)) != len(named):
            raise Refusal("P%s%02d names the same function twice: %s"
                          % (port, pin, funcs))

        pad = (ord(port) - ord("A")) * PINS_PER_PORT + pin
        if pad in pads:
            raise Refusal("P%s%02d appears twice in the spec" % (port, pin))
        pads[pad] = ("P%s%02d" % (port, pin), funcs)

    if not pads:
        raise Refusal("found no GPIO port summary rows at all -- is this the "
                      "right document?")
    return pads


def emit(socs, argv):
    """socs: (soc, pads, sources, overflows, conflicts) per SoC."""
    out = []
    w = out.append
    w("/* Generated by tools/gen_ingenic_padmux.py -- do not edit.")
    w(" *")
    w(" * The four device functions of each pad. Ingenic selects between them")
    w(" * with one bit each in the port's INT, MSK, PAT1 and PAT0 registers")
    w(" * rather than with a field, so these are names and nothing else:")
    w(" * src/hal/ingenic_padmux.c holds the registers.")
    w(" *")
    w(" * Regenerate with:")
    w(" *   %s" % " ".join(argv))
    w(" */")
    w("")
    w("#ifndef HAL_INGENIC_PADMUX_H")
    w("#define HAL_INGENIC_PADMUX_H")
    w("")
    w('#include "hal/ingenic_padmux_types.h"')
    w("")

    for soc, pads, sources, overflows, conflicts in socs:
        tag, nports, kind = SOCS[soc]
        w("/* %s: %d pads named, of %d in the SoC's %d ports. A pad no source"
          % (tag, len(pads), nports * PINS_PER_PORT, nports))
        w(" * names is not here at all, which is a different answer from one")
        w(" * whose four functions are all unused.")
        w(" *")
        if kind == "spec":
            w(" * From the GPIO spec's port summary tables: FUNCTION0..3 per")
            w(" * pad, named per wire.")
        else:
            w(" * From the vendor kernel's board file, which names a DEVICE")
            w(" * rather than a wire -- a 12-bit DVP bus is twelve pads all")
            w(" * called DVP_PA_12BIT -- and only covers what it wires up.")
            w(" * There is no GPIO spec for this part.")
        for path, digest in sources:
            w(" *   %s  %s" % (digest, path))
        if overflows or conflicts:
            w(" *")
            w(" * What that source got wrong, recorded so nobody has to find")
            w(" * it twice:")
            for line in overflows:
                w(" *   %s" % line)
                w(" *     the pins past the end are dropped")
            for line in conflicts:
                w(" *   %s" % line)
                w(" *     one function slot given up: the silicon cannot do")
                w(" *     both and the file does not say which is real")
        w(" */")
        w("static const ingenic_pad_t %s_pads[] = {" % tag)
        for pad in sorted(pads):
            name, funcs = pads[pad]
            w('    {%d, "%s", {%s}},'
              % (pad, name, ", ".join('"%s"' % f for f in funcs)))
        w("};")
        w("")
        w("static const ingenic_soc_t %s_padmux = {%s_pads, %d};"
          % (tag, tag, len(pads)))
        w("")

    w("#endif /* HAL_INGENIC_PADMUX_H */")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--soc", action="append", required=True,
                    choices=sorted(SOCS),
                    help="repeat with a matching --spec to put several SoCs "
                         "in one header")
    ap.add_argument("--spec", action="append", required=True,
                    help="the vendor GPIO spec (.pdf, or pdftotext -layout "
                         "output) for a spec-sourced SoC, or the kernel's "
                         "include/mach/platform.h for a platform-sourced one")
    ap.add_argument("--verify", metavar="HEADER")
    args = ap.parse_args()

    if len(args.soc) != len(args.spec):
        print("gen_ingenic_padmux: one --spec per --soc, in the same order",
              file=sys.stderr)
        return 2

    socs, argv = [], ["tools/gen_ingenic_padmux.py"]
    for soc, spec in zip(args.soc, args.spec):
        text = spec_text(spec)
        if SOCS[soc][2] == "platform":
            pads = parse_platform(text, soc)
        else:
            pads = parse(text, soc)
        socs.append((soc, pads, [(os.path.basename(spec), sha256(spec))],
                     list(OVERFLOWS), list(CONFLICTS)))
        argv += ["--soc", soc, "--spec", os.path.basename(spec)]

    text = emit(socs, argv)

    if args.verify:
        with open(args.verify, encoding="utf-8") as handle:
            have = handle.read()
        # the banner names the files it was run against; the data is what has
        # to match, and is what a reviewer can check
        if have.split(" */\n", 1)[-1] == text.split(" */\n", 1)[-1]:
            print("gen_ingenic_padmux: %s matches the sources" % args.verify)
            return 0
        print("gen_ingenic_padmux: %s does NOT match the sources" % args.verify,
              file=sys.stderr)
        return 1

    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Refusal as exc:
        print("gen_ingenic_padmux: %s" % exc, file=sys.stderr)
        sys.exit(1)
