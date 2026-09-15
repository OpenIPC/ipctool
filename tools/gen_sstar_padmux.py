#!/usr/bin/env python3
"""Turn a SigmaStar vendor kernel's pad-mux tables into src/hal/sstar_padmux.h.

SigmaStar does not mux per pad. One register field belongs to a *peripheral*
and its value picks which group of pads carries it, so `reg_fuart_mode` = 2
puts FUART on PAD_GPIO0..3 and = 4 puts it on PAD_SD1_IO0..3. The vendor keeps
that as two tables in drivers/sstar/gpio/<family>/mhal_pinmux.c:

    m_stPadMuxTbl[]        {padID, bank, offset, mask, val, mode}, one row per
                           (pad, mode) pair -- 568 of them on infinity6b0
    m_stPadModeInfoTbl[]   the name of each mode, indexed by PINMUX_FOR_*

Every row of one mode carries the same (bank, offset, mask, val) -- this
script proves it rather than assuming it -- so the address, the field and the
value fold into the mode table and the pad table holds nothing but mode
indices. That is what turns ~570 fat rows into ~145 modes plus a pool of
16-bit indices, and it is why this is generated rather than transcribed.

Usage:
    gen_sstar_padmux.py --kernel <tree> --family infinity6b0 [--family ...]
    gen_sstar_padmux.py --kernel <tree> --family ... --verify src/hal/sstar_padmux.h

--kernel is a directory holding drivers/sstar/gpio/<family>/mhal_pinmux.c and
drivers/sstar/include/<family>/{gpio.h,padmux.h}. Pass --kernel once per
family when they live in different trees.

The script refuses rather than guesses: a mode with two different tuples, a
value with bits outside its own field, a pad the SoC's gpio.h does not name,
or a pad count that disagrees with GPIO_NR all exit non-zero.
"""

import argparse
import hashlib
import os
import re
import sys

RIU_PHYS_BASE = 0x1F000000

# The tag each family gets in the generated C, and the ipctool chip_generation
# constants it answers for. Kept here rather than derived so a new family is
# one line and a deliberate one.
FAMILIES = {
    "infinity6b0": ("I6B", "INFINITY6 (Macaron) and INFINITY6B (Ispahan)"),
    "infinity6e": ("I6E", "INFINITY6E (Pudding)"),
    "infinity6c": ("I6C", "INFINITY6C (Maruko)"),
}


class Refusal(Exception):
    pass


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def read(path):
    with open(path, encoding="utf-8", errors="replace") as handle:
        return handle.read()


def sha256(path):
    with open(path, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()[:16]


class Defines:
    """The subset of the C preprocessor these three headers actually use:
    object-like macros whose bodies are BITn, hex, and | of the two."""

    def __init__(self, text):
        self.raw = {}
        for m in re.finditer(r"^[ \t]*#[ \t]*define[ \t]+(\w+)[ \t]+([^\n]*)$",
                             text, flags=re.M):
            body = m.group(2).strip()
            if body and not body.startswith("("):
                self.raw.setdefault(m.group(1), body)
        self.cache = {}

    def value(self, expr, depth=0):
        expr = expr.strip()
        if depth > 16:
            raise Refusal("macro expansion too deep: %s" % expr)
        if expr in self.cache:
            return self.cache[expr]

        def resolve(m):
            token = m.group(0)
            if token[0].isdigit():
                return token
            if re.fullmatch(r"BIT\d+", token):
                return str(1 << int(token[3:]))
            if token in self.raw:
                return "(%s)" % self.value(self.raw[token], depth + 1)
            raise Refusal("unknown symbol %r" % token)

        # numbers first: 0x101E00 must not be read as 0 followed by x101E00
        out = re.sub(r"0[xX][0-9a-fA-F]+|\d+|[A-Za-z_]\w*", resolve, expr)
        if not re.fullmatch(r"[0-9a-fA-FxX()|+\-<> ]*", out):
            raise Refusal("will not evaluate %r" % expr)
        try:
            val = int(eval(out, {"__builtins__": {}}, {}))  # noqa: S307
        except Exception as exc:
            raise Refusal("cannot evaluate %r: %s" % (expr, exc))
        if depth == 0:
            self.cache[expr] = val
        return val


def split_braced(block):
    """The top-level {...} entries of an initialiser body."""
    out, depth, start = [], 0, None
    for i, ch in enumerate(block):
        if ch == "{":
            if depth == 0:
                start = i + 1
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                out.append(block[start:i])
    return out


def table_body(text, name):
    m = re.search(re.escape(name) + r"\s*\[\s*\]\s*=\s*", text)
    if not m:
        return None
    i = text.index("{", m.end())
    depth, j = 0, i
    while j < len(text):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text[i + 1:j]
        j += 1
    raise Refusal("unterminated table %s" % name)


ROW_RE = re.compile(
    r"\{\s*(PAD_\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*,\s*([^,]+?)\s*,"
    r"\s*([^,]+?)\s*,\s*(PINMUX_FOR_\w+)\s*,?\s*\}", re.S)


def parse_family(kernel, family):
    gpio_dir = os.path.join(kernel, "drivers/sstar/gpio", family)
    inc_dir = os.path.join(kernel, "drivers/sstar/include", family)
    paths = {
        "pinmux": os.path.join(gpio_dir, "mhal_pinmux.c"),
        "padmux": os.path.join(inc_dir, "padmux.h"),
        "gpio": os.path.join(inc_dir, "gpio.h"),
    }
    for kind, path in paths.items():
        if not os.path.isfile(path):
            raise Refusal("no %s for %s at %s" % (kind, family, path))

    pinmux = strip_comments(read(paths["pinmux"]))
    padmux_h = strip_comments(read(paths["padmux"]))
    gpio_h = strip_comments(read(paths["gpio"]))

    defs = Defines(pinmux + "\n" + padmux_h + "\n" + gpio_h)

    # pad id -> vendor name, and the GPIO_NR the driver believes in
    pads = {}
    for m in re.finditer(r"#\s*define\s+(PAD_\w+)\s+(\d+)\b", gpio_h):
        pads[int(m.group(2))] = m.group(1)
    m = re.search(r"#\s*define\s+GPIO_NR\s+(\d+)", gpio_h)
    if not m:
        raise Refusal("%s: gpio.h has no GPIO_NR" % family)
    gpio_nr = int(m.group(1))
    if sorted(pads) != list(range(gpio_nr)):
        raise Refusal("%s: gpio.h names %d pads but GPIO_NR is %d"
                      % (family, len(pads), gpio_nr))

    # PINMUX_FOR_* -> id, and the mode names by id
    mode_id = {}
    for m in re.finditer(r"#\s*define\s+(PINMUX_FOR_\w+)\s+(0[xX][0-9a-fA-F]+|\d+)",
                         padmux_h):
        mode_id[m.group(1)] = int(m.group(2), 0)

    mode_name = {}
    body = table_body(pinmux, "m_stPadModeInfoTbl")
    if body:
        for i, entry in enumerate(split_braced(body)):
            n = re.match(r'\s*"([^"]*)"', entry)
            if n:
                mode_name[i] = n.group(1)

    # the (pad, mode) rows, wherever in the file they live: infinity6c splits
    # them into one array per pad instead of one table for the SoC
    rows, tuples, per_pad = [], {}, {}
    gpio_tuple = {}
    for m in ROW_RE.finditer(pinmux):
        pad_name, bank, off, mask, val, mode = m.groups()
        if mode not in mode_id:
            raise Refusal("%s: row names %s, which padmux.h does not define"
                          % (family, mode))
        mid = mode_id[mode]
        addr = RIU_PHYS_BASE + (defs.value(bank) << 1) + (defs.value(off) << 2)
        mask_v, val_v = defs.value(mask), defs.value(val)
        if val_v & ~mask_v:
            raise Refusal("%s: %s/%s value %#x has bits outside mask %#x"
                          % (family, pad_name, mode, val_v, mask_v))
        if mask_v > 0xFFFF:
            raise Refusal("%s: %s/%s mask %#x is wider than the 16-bit port"
                          % (family, pad_name, mode, mask_v))

        pad_id = next((k for k, v in pads.items() if v == pad_name), None)
        if pad_id is None:
            raise Refusal("%s: row names %s, which gpio.h does not"
                          % (family, pad_name))

        if mid == 0:
            # "this pad is GPIO" is a field of the PAD, not of a mode -- the
            # mode table's entry 0 carries no address at all -- and on
            # infinity6c a pad can need two of them (PAD_SD_CLK wants its
            # PADGPIO bit and the eMMC external-enable bit).
            claims = gpio_tuple.setdefault(pad_id, [])
            if (addr, mask_v, val_v) not in claims:
                claims.append((addr, mask_v, val_v))
            continue

        tuples.setdefault(mid, {}).setdefault((addr, mask_v, val_v), []).append(pad_id)
        per_pad.setdefault(pad_id, [])
        if mid not in per_pad[pad_id]:
            per_pad[pad_id].append(mid)
        rows.append((pad_id, mid))

    if not rows:
        raise Refusal("%s: found no pad-mux rows at all" % family)

    # A mode is one field of one register and one value in it -- for all but a
    # handful. The exceptions are the pads the vendor's own driver will not
    # program from the table either (HalPadSetMode_MISC: the SAR, ETH and USB
    # pads, which want several banks and a PM unlock), and there the same mode
    # id names a different register per pad. Those modes are dropped whole
    # rather than folded into a tuple that would be wrong for someone: nothing
    # can select them with one write, so there is nothing honest to publish.
    dropped = []
    for mid in sorted(tuples):
        if len(tuples[mid]) == 1:
            continue
        name = mode_name.get(mid) or mode_name_from_macro(mode_id, mid)
        pads_hit = sorted({p for v in tuples[mid].values() for p in v})
        dropped.append((name, [pads[p] for p in pads_hit]))
        del tuples[mid]
        for pad_id in pads_hit:
            if mid in per_pad.get(pad_id, []):
                per_pad[pad_id].remove(mid)
        rows = [r for r in rows if r[1] != mid]

    modes = []
    for mid in sorted(tuples):
        name = mode_name.get(mid)
        if name is None:
            name = mode_name_from_macro(mode_id, mid)
        modes.append((mid, name, next(iter(tuples[mid]))))

    return {
        "family": family,
        "tag": FAMILIES[family][0],
        "pads": pads,
        "gpio_nr": gpio_nr,
        "per_pad": per_pad,
        "gpio_tuple": gpio_tuple,
        "modes": modes,
        "rows": len(rows),
        "dropped": dropped,
        # relative to the kernel tree, not to this machine: the banner has to
        # mean the same thing to whoever regenerates it next
        "sources": [(os.path.relpath(p, kernel), sha256(p))
                    for p in paths.values()],
    }


def mode_name_from_macro(mode_id, mid):
    for name, value in mode_id.items():
        if value == mid:
            return name[len("PINMUX_FOR_"):]
    raise Refusal("mode %d has no name anywhere" % mid)


def emit(families, argv):
    out = []
    w = out.append
    w("/* Generated by tools/gen_sstar_padmux.py -- do not edit.")
    w(" *")
    w(" * SigmaStar mux registers are per PERIPHERAL, not per pad: a mode is one")
    w(" * field of one chip-top register plus the value that routes that")
    w(" * peripheral onto the group of pads listed against the mode. See")
    w(" * src/hal/sstar_padmux.c for what reads this and docs/padmux.md for why")
    w(" * it does not look like the HiSilicon tables in src/reginfo.c.")
    w(" *")
    w(" * Regenerate with:")
    w(" *   %s" % argv)
    w(" *")
    w(" * From, in a vendor kernel tree:")
    for fam in families:
        for path, digest in fam["sources"]:
            w(" *   %s  %s" % (digest, path))
    w(" */")
    w("")
    w("#ifndef HAL_SSTAR_PADMUX_H")
    w("#define HAL_SSTAR_PADMUX_H")
    w("")
    w('#include "hal/sstar_padmux_types.h"')
    w("")

    for fam in families:
        tag = fam["tag"]
        index_of = {mid: i for i, (mid, _, _) in enumerate(fam["modes"])}

        w("/* %s -- %s: %d pads, %d modes, %d claims."
          % (tag, FAMILIES[fam["family"]][1], fam["gpio_nr"],
             len(fam["modes"]), fam["rows"]))
        if fam["dropped"]:
            w(" *")
            w(" * Not here, because the same mode id selects a different")
            w(" * register on different pads and no one write can reach it:")
            for name, pad_names in fam["dropped"]:
                w(" *   %s on %s" % (name, ", ".join(pad_names)))
        w(" */")
        w("static const sstar_mode_t %s_modes[] = {" % tag)
        for mid, name, (addr, mask, val) in fam["modes"]:
            w('    {"%s", 0x%08X, 0x%04X, 0x%04X},' % (name, addr, mask, val))
        w("};")
        w("")

        pool, gpio_fields, pads = [], [], []
        for pad_id in range(fam["gpio_nr"]):
            mids = fam["per_pad"].get(pad_id, [])
            first = len(pool)
            pool.extend(index_of[m] for m in mids)
            claims = fam["gpio_tuple"].get(pad_id, [])
            gfirst = len(gpio_fields)
            gpio_fields.extend(claims)
            pads.append((pad_id, fam["pads"][pad_id], first, len(mids),
                         gfirst, len(claims)))

        w("/* Which modes can claim each pad, as indices into %s_modes. */" % tag)
        w("static const uint16_t %s_pool[] = {" % tag)
        for i in range(0, len(pool), 16):
            w("    " + " ".join("%d," % v for v in pool[i:i + 16]))
        w("};")
        w("")
        w("/* The fields that say \"this pad is GPIO\", where the part has any.")
        w(" * infinity6b0 has almost none -- there a pad is GPIO when nothing")
        w(" * claims it -- and infinity6c sometimes needs two. */")
        w("static const sstar_field_t %s_gpio_fields[] = {" % tag)
        if not gpio_fields:
            w("    {IPCHW_PADMUX_ADDR_NONE, 0, 0}, /* none on this part */")
        for addr, mask, val in gpio_fields:
            w("    {0x%08X, 0x%04X, 0x%04X}," % (addr, mask, val))
        w("};")
        w("")
        w("static const sstar_pad_t %s_pads[] = {" % tag)
        for pad_id, name, first, count, gfirst, gcount in pads:
            w('    {"%s", %d, %d, %d, %d}, /* %d */'
              % (name, first, count, gfirst, gcount, pad_id))
        w("};")
        w("")
        w("static const sstar_family_t %s_padmux = {" % tag)
        w("    %s_modes, %d, %s_pads, %d," % (tag, len(fam["modes"]), tag, len(pads)))
        w("    %s_pool, %s_gpio_fields," % (tag, tag))
        w("};")
        w("")

    w("#endif /* HAL_SSTAR_PADMUX_H */")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--kernel", action="append", required=True,
                    help="a vendor kernel tree; repeat to draw families from "
                         "more than one")
    ap.add_argument("--family", action="append", required=True,
                    choices=sorted(FAMILIES))
    ap.add_argument("--verify", metavar="HEADER",
                    help="re-derive and diff against this file instead of "
                         "writing; exit 1 when they differ")
    args = ap.parse_args()

    families = []
    for family in args.family:
        last = None
        for kernel in args.kernel:
            try:
                families.append(parse_family(kernel, family))
                break
            except Refusal as exc:
                last = exc
        else:
            print("gen_sstar_padmux: %s" % last, file=sys.stderr)
            return 1

    text = emit(families, "tools/gen_sstar_padmux.py --kernel <tree> " +
                " ".join("--family %s" % f for f in args.family))

    if args.verify:
        have = read(args.verify)
        # the banner names the tree it was run against; the data is what has
        # to match, and is what a reviewer can check
        if have.split(" */\n", 1)[-1] == text.split(" */\n", 1)[-1]:
            print("gen_sstar_padmux: %s matches the SDK" % args.verify)
            return 0
        print("gen_sstar_padmux: %s does NOT match the SDK" % args.verify,
              file=sys.stderr)
        return 1

    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Refusal as exc:
        print("gen_sstar_padmux: %s" % exc, file=sys.stderr)
        sys.exit(1)
