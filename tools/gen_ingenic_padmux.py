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

SOCS = {
    "T31": ("T31", "T31", 3),  # tag, chip_generation name, ports
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
    return name.upper()


def parse(text, soc):
    _, _, nports = SOCS[soc]
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


def emit(soc, pads, sources, argv):
    tag = SOCS[soc][0]
    out = []
    w = out.append
    w("/* Generated by tools/gen_ingenic_padmux.py -- do not edit.")
    w(" *")
    w(" * The four device functions of each pad, as the vendor's GPIO spec")
    w(" * tables them. Ingenic selects between them with one bit each in the")
    w(" * port's INT, MSK, PAT1 and PAT0 registers rather than with a field, so")
    w(" * these are names and nothing else: src/hal/ingenic_padmux.c holds the")
    w(" * registers.")
    w(" *")
    w(" * Regenerate with:")
    w(" *   %s" % " ".join(argv))
    w(" *")
    w(" * Sources:")
    for path, digest in sources:
        w(" *   %s  %s" % (digest, path))
    w(" */")
    w("")
    w("#ifndef HAL_INGENIC_PADMUX_H")
    w("#define HAL_INGENIC_PADMUX_H")
    w("")
    w('#include "hal/ingenic_padmux_types.h"')
    w("")
    w("/* %s: %d pads the package brings out, of %d in the ports. A pad the"
      % (tag, len(pads), SOCS[soc][2] * PINS_PER_PORT))
    w(" * spec gives no row for is not here at all, which is a different answer")
    w(" * from one whose four functions are all unused. */")
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
    ap.add_argument("--spec", required=True,
                    help="the vendor GPIO spec, .pdf or pdftotext -layout output")
    ap.add_argument("--soc", required=True, choices=sorted(SOCS))
    ap.add_argument("--verify", metavar="HEADER")
    args = ap.parse_args()

    pads = parse(spec_text(args.spec), args.soc)
    text = emit(args.soc, pads, [(args.spec, sha256(args.spec))],
                ["tools/gen_ingenic_padmux.py", "--spec",
                 os.path.basename(args.spec), "--soc", args.soc])

    if args.verify:
        with open(args.verify, encoding="utf-8") as handle:
            have = handle.read()
        # the provenance banner names the path it was run with; compare the
        # data, which is what has to match
        if have.split(" */\n", 1)[-1] == text.split(" */\n", 1)[-1]:
            print("gen_ingenic_padmux: %s matches the spec" % args.verify)
            return 0
        print("gen_ingenic_padmux: %s does NOT match the spec" % args.verify,
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
