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

def pmx(name):
    """The PMX_ identifier a mode or pad name is spelled as in the table.

    The tables hold offsets into padmux_names rather than pointers -- see
    src/hal/sstar_padmux_types.h -- and tools/gen_padmux_names.py is what
    turns these identifiers into that blob. Its mangling and this one have to
    agree; its --selftest is what pins the rule, and a disagreement is a
    compile error in the generated header rather than a wrong name.
    """
    ident = re.sub(r"[^A-Za-z0-9_]", "__", name)
    if not ident or ident[0].isdigit():
        ident = "_" + ident
    return "PMX_" + ident

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


def function_body(text, name):
    """The braced body of a function definition, or None."""
    m = re.search(r"\b" + re.escape(name) + r"\s*\([^;{}]*\)\s*\{", text)
    if not m:
        return None
    i = text.index("{", m.end() - 1)
    depth, j = 0, i
    while j < len(text):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text[i + 1:j]
        j += 1
    raise Refusal("unterminated function %s" % name)


LABEL_RE = re.compile(r"\bcase\s+(PAD_\w+)\s*:|\bdefault\s*:")
RIUA_OPEN_RE = re.compile(r"_RIUA_16BIT\s*\(")


def macro_args(text, start):
    r"""The arguments of a macro call whose "(" ends at `start`.

    Split on top-level commas only. A regex cannot do this: the offsets are
    written relative to the first pad of a group, so
    `REG_FUART_RX_GPIO_MODE+(u32PadID-PAD_FUART_RX)` has a nested pair, and a
    lazy `(.+?)\)` stops at the inner one and hands back an expression with
    an unbalanced parenthesis.
    """
    args, cur, depth, i = [], "", 1, start
    while i < len(text):
        ch = text[i]
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                args.append(cur)
                return [a.strip() for a in args], i + 1
        if depth == 1 and ch == ",":
            args.append(cur)
            cur = ""
        else:
            cur += ch
        i += 1
    raise Refusal("unterminated macro call")


def parse_misc(pinmux, pads, defs, family):
    """Which pads HalPadSetMode_MISC() programs, and through which registers.

    m_stPadMuxTbl is not the whole truth. Some pads are muxed from banks the
    table never mentions -- an Ethernet pair by REG_ETH_GPIO_EN in ALBANY2, a
    USB pair by the UTMI0 power-down bits -- and the vendor routes exactly
    those through HalPadSetMode_MISC() instead of HalPadSetMode_General().
    Their rows in the table are still there, and on infinity6e they are
    copy-paste from PAD_SPI_HLD: PAD_ETH_RN through PAD_USB2_DP each claim
    SPIHOLDN_MODE and EMMC0_8B_MODE_1 through the SPI pad's own fields. Taken
    at face value that reports all six pads as carrying whatever the flash
    HOLD pin carries, and offers to put eMMC data lines on the Ethernet
    magnetics.

    So MISC is parsed for what it actually writes per pad. The caller drops a
    pad's table rows when the two disagree completely -- see misc_conflicts().
    A pad whose MISC path writes registers the table DOES name is left alone:
    the FUART pads are muxed through their own PADTOP fields either way, and
    MISC only adds a side effect the table cannot express.

    Returns {pad id: set of physical addresses}.
    """
    body = function_body(pinmux, "HalPadSetMode_MISC")
    if body is None:
        return {}

    by_name = {v: k for k, v in pads.items()}
    labels = list(LABEL_RE.finditer(body))
    out = {}
    for i, lab in enumerate(labels):
        if lab.group(1) is None:
            continue
        end = labels[i + 1].start() if i + 1 < len(labels) else len(body)
        seg = body[lab.end():end]
        if not seg.strip():
            continue  # a bare label falling through into the next block

        # every label that falls through into this block shares it
        group, j = [lab.group(1)], i - 1
        while (j >= 0 and labels[j].group(1) is not None
               and not body[labels[j].end():labels[j + 1].start()].strip()):
            group.append(labels[j].group(1))
            j -= 1

        for pad_name in group:
            pad_id = by_name.get(pad_name)
            if pad_id is None:
                raise Refusal("%s: HalPadSetMode_MISC names %s, which gpio.h "
                              "does not" % (family, pad_name))
            pos = 0
            while True:
                m = RIUA_OPEN_RE.search(seg, pos)
                if m is None:
                    break
                args, pos = macro_args(seg, m.end())
                if len(args) != 2:
                    raise Refusal("%s: _RIUA_16BIT near %s takes %d arguments"
                                  % (family, pad_name, len(args)))
                bank, off = args
                # offsets are written relative to the first pad of the group,
                # e.g. REG_FUART_RX_GPIO_MODE+(u32PadID-PAD_FUART_RX)
                expr = off.replace("u32PadID", str(pad_id))
                try:
                    addr = (RIU_PHYS_BASE + (defs.value(bank) << 1)
                            + (defs.value(expr) << 2))
                except Refusal as exc:
                    # Not skippable. A register this parser drops shrinks the
                    # pad's MISC set, and a pad whose whole set is dropped
                    # never reaches misc_conflicts() at all -- so the rows
                    # this check exists to catch would be kept, silently.
                    raise Refusal("%s: cannot resolve the register "
                                  "HalPadSetMode_MISC() writes for %s "
                                  "(%s, %s): %s"
                                  % (family, pad_name, bank, off, exc))
                out.setdefault(pad_id, set()).add(addr)
    return out


def misc_conflicts(misc, per_pad, tuples, gpio_tuple, pads, family):
    """The pads whose table rows describe a register MISC never touches.

    Overlap, not containment: the vendor is inconsistent about which of a
    group's modes it lists (infinity6e gives PWM9_MODE_2 to PAD_FUART_CTS but
    not to PAD_FUART_RX, though MISC programs it for both), so requiring the
    table to be complete would throw away good rows. One shared register is
    enough to say the rows are about this pad.
    """
    bad = []
    for pad_id, regs in sorted(misc.items()):
        named = {t[0] for t in gpio_tuple.get(pad_id, [])}
        for mid in per_pad.get(pad_id, []):
            for tup, tup_pads in tuples.get(mid, {}).items():
                if pad_id in tup_pads:
                    named.add(tup[0])
        if named and not (named & regs):
            bad.append(pad_id)
    return bad


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

    # A pad the vendor muxes from banks m_stPadMuxTbl never names keeps rows
    # that are about some other pad. Drop them before the pass below, not
    # after: a mode that looked like it had two registers only because one of
    # them came from such a row is a perfectly ordinary mode once the row is
    # gone, and the pads that really do offer it should keep it.
    misc = parse_misc(pinmux, pads, defs, family)
    misc_dropped = []
    for pad_id in misc_conflicts(misc, per_pad, tuples, gpio_tuple, pads,
                                 family):
        misc_dropped.append(pads[pad_id])
        for mid in list(per_pad.get(pad_id, [])):
            for tup in list(tuples.get(mid, {})):
                if pad_id in tuples[mid][tup]:
                    tuples[mid][tup].remove(pad_id)
                    if not tuples[mid][tup]:
                        del tuples[mid][tup]
            if not tuples.get(mid):
                tuples.pop(mid, None)
        per_pad[pad_id] = []
        gpio_tuple.pop(pad_id, None)
        rows = [r for r in rows if r[0] != pad_id]

    if not rows:
        raise Refusal("%s: every pad-mux row belongs to a MISC pad" % family)

    # A mode is one field of one register and one value in it -- for all but a
    # handful. The exceptions are the pads the vendor's own driver will not
    # program from the table either (HalPadSetMode_MISC: the SAR, ETH and USB
    # pads, which want several banks and a PM unlock), and there the same mode
    # id names a different register per pad. Those modes are dropped whole
    # rather than folded into a tuple that would be wrong for someone: nothing
    # can select them with one write, so there is nothing honest to publish.
    dropped = []
    unnamed = {}
    for mid in sorted(tuples):
        if len(tuples[mid]) == 1:
            continue
        name = mode_name.get(mid) or mode_name_from_macro(mode_id, mid)
        pads_hit = sorted({p for v in tuples[mid].values() for p in v})
        dropped.append((name, [pads[p] for p in pads_hit]))

        # The mode goes, but what it would look like on each pad does not.
        # A claim selected by a NON-ZERO value can be tested on its own: if
        # that field ever reads back its value, something is on the pad that
        # this build can no longer name, and saying so beats reporting the pad
        # as free. A claim selected by ZERO cannot -- an idle register reads
        # the same -- so those are left to the pad's GPIO fields instead.
        for tup, tup_pads in tuples[mid].items():
            if tup[2] == 0:
                continue
            for pad_id in tup_pads:
                # A field that already means "this pad is GPIO" cannot also
                # mean "something unnameable is on it". The SAR pads are like
                # that: the vendor lists one field and value under both
                # GPIO_MODE and OTP_TEST_1, so keeping it here would make
                # setting the pad to GPIO read back as unnameable.
                if tup in gpio_tuple.get(pad_id, []):
                    continue
                if tup not in unnamed.setdefault(pad_id, []):
                    unnamed[pad_id].append(tup)

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
        "misc_dropped": misc_dropped,
        "unnamed": unnamed,
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


def wrap_command(argv):
    """The invocation, one option pair to a line.

    It goes in a C comment, and a comment long enough to be reflowed comes out
    the other side split at a space, which is not a command any more. Shell
    continuations keep it one.
    """
    lines, pair = [argv[0]], []
    for arg in argv[1:]:
        pair.append(arg)
        if len(pair) == 2:
            lines.append("    " + " ".join(pair))
            pair = []
    if pair:
        lines.append("    " + " ".join(pair))
    return [l + " \\" for l in lines[:-1]] + lines[-1:]


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
    for line in wrap_command(argv.split()):
        w(" *   %s" % line)
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
        if fam["misc_dropped"]:
            w(" *")
            w(" * Listed with no modes at all, because the vendor muxes them")
            w(" * from a bank this table does not reach (HalPadSetMode_MISC)")
            w(" * and the rows they do carry name another pad's registers:")
            w(" *   %s" % ", ".join(fam["misc_dropped"]))
        w(" */")
        w("static const sstar_mode_t %s_modes[] = {" % tag)
        for mid, name, (addr, mask, val) in fam["modes"]:
            w('    {%s, 0x%08X, 0x%04X, 0x%04X},' % (pmx(name), addr, mask, val))
        w("};")
        w("")

        pool, gpio_fields, unnamed_fields, pads = [], [], [], []
        for pad_id in range(fam["gpio_nr"]):
            mids = fam["per_pad"].get(pad_id, [])
            first = len(pool)
            pool.extend(index_of[m] for m in mids)
            claims = fam["gpio_tuple"].get(pad_id, [])
            gfirst = len(gpio_fields)
            gpio_fields.extend(claims)
            unn = fam["unnamed"].get(pad_id, [])
            ufirst = len(unnamed_fields)
            unnamed_fields.extend(unn)
            pads.append((pad_id, fam["pads"][pad_id], first, len(mids),
                         gfirst, len(claims), ufirst, len(unn)))

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
        w("/* Fields that would mean a mode this build had to drop is live on")
        w(" * the pad. Not a function it can name -- just a reason not to call")
        w(" * the pad free. */")
        w("static const sstar_field_t %s_unnamed_fields[] = {" % tag)
        if not unnamed_fields:
            w("    {IPCHW_PADMUX_ADDR_NONE, 0, 0}, /* nothing dropped here */")
        for addr, mask, val in unnamed_fields:
            w("    {0x%08X, 0x%04X, 0x%04X}," % (addr, mask, val))
        w("};")
        w("")
        w("static const sstar_pad_t %s_pads[] = {" % tag)
        for pad_id, name, first, count, gfirst, gcount, ufirst, ucount in pads:
            w('    {%s, %d, %d, %d, %d, %d, %d}, /* %d */'
              % (pmx(name), first, count, gfirst, gcount, ufirst, ucount,
                 pad_id))
        w("};")
        w("")
        w("static const sstar_family_t %s_padmux = {" % tag)
        w("    %s_modes, %d, %s_pads, %d," % (tag, len(fam["modes"]), tag, len(pads)))
        w("    %s_pool, %s_gpio_fields, %s_unnamed_fields," % (tag, tag, tag))
        w("};")
        w("")

    w("#endif /* HAL_SSTAR_PADMUX_H */")
    return "\n".join(out) + "\n"


SELFTEST_SRC = """
#define PADTOP_BANK   0x103C00
#define UTMI0_BANK    0x142100
#define PAD_FUART_RX  46
#define PAD_FUART_TX  47
#define PAD_USB2_DM   125
#define PAD_USB2_DP   126
#define REG_FUART_RX_GPIO_MODE  0x60
#define REG_PWM0_MODE           0x50
#define REG_UTMI0_GPIO_EN       0x1f

static S32 HalPadSetMode_MISC(U32 u32PadID, U32 u32Mode)
{
    switch(u32PadID)
    {
    case PAD_FUART_RX:
    case PAD_FUART_TX:
        _GPIO_W_WORD_MASK(_RIUA_16BIT(PADTOP_BANK,REG_FUART_RX_GPIO_MODE+(u32PadID-PAD_FUART_RX)), BIT3, MASK);
        _GPIO_W_WORD_MASK(_RIUA_16BIT(PADTOP_BANK,REG_PWM0_MODE), 0, MASK);
        break;
    case PAD_USB2_DM:
    case PAD_USB2_DP:
        _GPIO_W_WORD_MASK(_RIUA_16BIT(UTMI0_BANK,REG_UTMI0_GPIO_EN), 0, MASK);
        break;
    default:
        break;
    }
    return 0;
}
"""


def selftest():
    """Exercise the MISC parse without an SDK, so CI can run it.

    Everything here is a shape that has already gone wrong once. The nested
    offset is the one that matters: a lazy regex stops at its inner ")" and
    hands back an unbalanced expression, which then fails to evaluate -- and
    when the failure was skipped rather than raised, a pad whose whole
    register set was dropped never reached misc_conflicts() at all, so the
    bogus rows this check exists to catch stayed in the table.
    """
    fails = []

    def check(ok, what):
        if not ok:
            fails.append(what)

    # 1. the arguments come back balanced, nesting and all
    text = "_RIUA_16BIT(A_BANK,REG_X+(u32PadID-PAD_Y))"
    args, end = macro_args(text, text.index("(") + 1)
    check(args == ["A_BANK", "REG_X+(u32PadID-PAD_Y)"], "macro_args nesting: %r" % (args,))
    check(end == len(text), "macro_args end: %d of %d" % (end, len(text)))

    pads = {46: "PAD_FUART_RX", 47: "PAD_FUART_TX",
            125: "PAD_USB2_DM", 126: "PAD_USB2_DP"}
    defs = Defines(SELFTEST_SRC)
    try:
        misc = parse_misc(strip_comments(SELFTEST_SRC), pads, defs, "selftest")
    except Refusal as exc:
        print("gen_sstar_padmux selftest: the fixture would not parse: %s"
              % exc, file=sys.stderr)
        return 1

    # 2. every pad of a fall-through case group gets that group's registers,
    #    and the per-pad offset really is per pad
    fuart_gpio = RIU_PHYS_BASE + (0x103C00 << 1)
    check(misc.get(46) == {fuart_gpio + (0x60 << 2), fuart_gpio + (0x50 << 2)},
          "pad 46 regs: %r" % (misc.get(46),))
    check(misc.get(47) == {fuart_gpio + (0x61 << 2), fuart_gpio + (0x50 << 2)},
          "pad 47 regs: %r" % (misc.get(47),))
    utmi = RIU_PHYS_BASE + (0x142100 << 1) + (0x1F << 2)
    check(misc.get(125) == {utmi} and misc.get(126) == {utmi},
          "USB regs: %r %r" % (misc.get(125), misc.get(126)))

    # 3. a register that cannot be resolved is a refusal, never a silent skip
    broken = SELFTEST_SRC.replace("REG_PWM0_MODE), 0", "REG_NOT_DEFINED), 0")
    try:
        parse_misc(strip_comments(broken), pads, Defines(broken), "selftest")
        check(False, "an unresolvable MISC register was skipped, not refused")
    except Refusal:
        pass

    # 4. overlap keeps a pad, no overlap drops it
    tuples = {1: {(fuart_gpio + (0x50 << 2), 0x7, 0x1): [46]},
              2: {(fuart_gpio + (0x99 << 2), 0x7, 0x1): [125]}}
    per_pad = {46: [1], 125: [2]}
    bad = misc_conflicts(misc, per_pad, tuples, {}, pads, "selftest")
    check(bad == [125], "misc_conflicts picked %r, wanted [125]" % (bad,))

    for f in fails:
        print("gen_sstar_padmux selftest: %s" % f, file=sys.stderr)
    if not fails:
        print("gen_sstar_padmux: selftest passed")
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--selftest", action="store_true",
                    help="check the vendor-source parsing against a built-in "
                         "fixture and exit; needs no SDK, so CI can run it")
    # Not required=True: --selftest is a complete invocation on its own, and
    # argparse has to see the whole command line either way so that a typo
    # next to --selftest is an error rather than a pass.
    ap.add_argument("--kernel", action="append",
                    help="a vendor kernel tree; repeat to draw families from "
                         "more than one")
    ap.add_argument("--family", action="append",
                    choices=sorted(FAMILIES))
    ap.add_argument("--verify", metavar="HEADER",
                    help="re-derive and diff against this file instead of "
                         "writing; exit 1 when they differ")
    args = ap.parse_args()

    # --selftest stands alone: it parses a fixture, not a tree
    if args.selftest:
        return selftest()
    missing = [n for n in ("kernel", "family") if not getattr(args, n)]
    if missing:
        ap.error("--%s is required" % ", --".join(missing))

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
