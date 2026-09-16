#!/usr/bin/env python3
"""Check a HiSilicon pad-mux table in src/reginfo.c against its data sheet.

The HiSilicon rows are hand-entered, and `funcs[i]` means "selector value i".
A data sheet states that in two places which do NOT say the same thing:

  * chapter 2.3, "Pin Multiplexing Control Registers", gives each register's
    bit field and spells out every value: "000: GPIO2_0", "001: RMII_CLK",
    "011: VO_CLK", "100: SDIO1_CCLK_OUT". Note the absent 010. This is the
    encoding, and it is what this script reads.

  * chapter 2.4, "Software Multiplexed Pins", lists the same alternatives in
    columns headed "Multiplexed Signal 0", "Multiplexed Signal 1" and
    "Multiplexed Signals 2-4" -- by POSITION, with the holes closed up. Read
    as selector values it is wrong, and reading it that way is how
    Hi3518EV20X and Hi3516CV200 came to report SDIO1 one value low on nine
    pins each (issue #135).

So: a hole in the middle of a register's values has to appear in the table as
"reserved", or every function after it is off by one. reginfo.c already
treats "reserved" as a hole -- it is skipped by the walks and refused by
ipchw_padmux_set() -- so the fix is to put the holes back.

Usage:
    tools/check_hisi_padmux.py --datasheet <pdf|txt> --prefix EV20X_ \\
        --base 0x200f0000 [--soc "Hi3518E V20X"] [--fix]

A data sheet may cover several SoCs back to back, each with its own chapter 2
(the Hi3518EV20X/Hi3516CV200 one does: a 192-pin part and a 273-pin part).
--soc picks one; without it, every chapter found is listed and the script
exits non-zero so the choice is deliberate.

Exit status is 0 only when the table and the chapter agree -- or, under
--fix, when every disagreement was a missing hole and all of them were
rewritten. Anything this tool will not repair on its own (a row the chapter
does not describe, an address that is not base + 4n, a renamed function, a
register with no row at all) stops --fix from writing at all: each of those
is at least as likely to mean the wrong document, --soc or --base as a wrong
table.

Needs pdftotext (poppler) for a PDF. Data sheets are not in this repo, so CI
cannot run this; it is for whoever has the document.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

NOISE = re.compile(r"HiSilicon Proprietary|Copyright ©|Issue \d+ \(")
# "10: RMII_CLK_OUT/MII_TX_CLK" -- one pad value, two names for it depending
# on the mode the block is in. The tables carry the slashed spelling verbatim,
# so the parse has to keep it or every such row reads as a rename.
VALUE = re.compile(r"\b([01]{1,4}):\s*([A-Za-z_][\w/]*)")
# A section heading, and only that. The same name is repeated deeply indented
# inside each register's bit-layout diagram, and the diagram's copy is not
# safe to key on: in the Hi3518EV100 sheet it is pushed out to column ~140 and
# comes back from pdftotext with its trailing digits clipped, so
# "muxctrl_reg22" reads as "muxctrl_reg2" and hands that register's values to
# a register five pages earlier. Headings start at column 0.
HEAD = re.compile(r"muxctrl_reg(\d+)\s*")
MUX = re.compile(r"MUXCTRL\(\s*(\w+)\s*,\s*(0x[0-9a-fA-F]+)\s*,(.*?)\)\s*$",
                 re.S | re.M)


def as_text(path):
    if not path.lower().endswith(".pdf"):
        return open(path, encoding="utf-8", errors="replace").read()
    if not shutil.which("pdftotext"):
        sys.exit("pdftotext not found; install poppler or pass a .txt")
    with tempfile.NamedTemporaryFile(suffix=".txt", delete=False) as tmp:
        out = tmp.name
    try:
        subprocess.run(["pdftotext", "-layout", path, out], check=True)
        return open(out, encoding="utf-8", errors="replace").read()
    finally:
        os.unlink(out)


def chapters(lines):
    """(soc name, lo, hi) for every pin-mux register chapter in the document."""
    starts = [i for i, l in enumerate(lines)
              if l.startswith("2.3 Pin Multiplexing Control Registers")]
    ends = [i for i, l in enumerate(lines)
            if l.startswith("2.4 Software Multiplexed Pins")]
    pkg = [i for i, l in enumerate(lines) if l.startswith("2.1.1 Package")]
    out = []
    for s in starts:
        later = [x for x in ends if x > s]
        if not later:
            continue
        before = [x for x in pkg if x < s]
        who = "?"
        if before:
            m = re.search(r"(Hi\w+(?: \w+)?) uses the",
                          " ".join(lines[before[-1]:before[-1] + 8]))
            if m:
                who = m.group(1)
        out.append((who, s, min(later)))
    return out


def registers(lines, lo, hi):
    """{register number: [function per selector value]}"""
    heads = [(i, int(HEAD.fullmatch(lines[i]).group(1)))
             for i in range(lo, hi) if HEAD.fullmatch(lines[i])]
    out = {}
    for k, (i, num) in enumerate(heads):
        end = heads[k + 1][0] if k + 1 < len(heads) else hi
        vals, width = {}, None
        for l in lines[i:end]:
            if NOISE.search(l):
                continue
            for w in re.finditer(r"\[(\d+):0\]", l):
                width = width if width is not None else int(w.group(1)) + 1
            if width is None and re.search(r"\[0\]\s+RW", l):
                width = 1
            for m in VALUE.finditer(l):
                # "11: reserved" spelled out is the same statement as leaving
                # 11 off the list, and the tables only carry a hole when a
                # later value needs its index. Recording it as a function
                # would make every such row read as one entry too long.
                if m.group(2).lower() == "reserved":
                    continue
                vals.setdefault(int(m.group(1), 2), m.group(2))
        if not vals:
            continue
        if width is not None and max(vals) >= 1 << width:
            sys.exit("muxctrl_reg%d: value %d does not fit a %d-bit field; "
                     "the parse is wrong, not the table" % (num, max(vals), width))
        if num not in out or len(vals) > len(out[num]):
            out[num] = [vals.get(i, "reserved") for i in range(max(vals) + 1)]
    return out


# What is wrong with one row, if anything. Kept apart from the printing so it
# can be exercised without a data sheet -- see selftest().
OK, HOLE, RENAME, UNALIGNED, NOSUCH = "ok", "hole", "rename", "unaligned", "nosuch"


def classify(addr, funcs, ds, base):
    """(verdict, register number or None, the data sheet's row or None)."""
    if addr < base or (addr - base) % 4:
        return UNALIGNED, None, None
    num = (addr - base) // 4
    want = ds.get(num)
    if want is None:
        return NOSUCH, num, None
    if funcs == want:
        return OK, num, want
    # Only a hole is repairable. A differing NAME means the parse, the --soc
    # or the --base is at least as suspect as the table.
    if [f for f in funcs if f != "reserved"] != \
            [f for f in want if f != "reserved"]:
        return RENAME, num, want
    return HOLE, num, want


def table_rows(src, prefix):
    out = {}
    for m in MUX.finditer(src):
        if m.group(1).startswith(prefix):
            out[int(m.group(2), 16)] = (m.group(1), m.group(2),
                                        re.findall(r'"([^"]*)"', m.group(3)),
                                        m.group(0))
    return out


# A synthetic data sheet in the shape pdftotext -layout actually produces.
# Every oddity in it is one that has already cost a wrong answer, so the
# layout matters as much as the content: the column-0 headings, the indented
# copy of the name inside the bit diagram, the page break landing in the
# middle of a value list, and the widths that vary from 1 to 3 bits.
SELFTEST_DOC = """
2.1.1 Package
                  HiFoo V100 uses the lead thin & fine-pitch ball grid array
2.3 Pin Multiplexing Control Registers
2.3.1 Register Summary
muxctrl_reg0
                         muxctrl_reg0 is a multiplexing control register.
                          0x000                muxctrl_reg0                0x00000000
                                                                                 muxctrl_reg0
        Bits         Access       Name                     Description
        [31:3]       RO           reserved                 Reserved
        [2:0]        RW           muxctrl_reg0             000: GPIO0_0
                                                           001: AAA
                                              HiSilicon Proprietary and Confidential
Issue 03 (2018-01-25)                                                            2-45
                                           Copyright (c) HiSilicon
                                                           011: BBB
                                                           100: CCC
                                                           Other values: reserved
muxctrl_reg1
                         muxctrl_reg1 is a multiplexing control register.
                          0x004                muxctrl_reg1                0x00000000
                                                                                 muxctrl_reg1
        [31:1]       RO           reserved                 Reserved
        [0]          RW           muxctrl_reg1             0: GPIO0_1
                                                           1: DDD
muxctrl_reg2
                         muxctrl_reg2 is a multiplexing control register.
                          0x008                muxctrl_reg2                0x00000000
                                                                                 muxctrl_reg2
        [31:2]       RO           reserved                 Reserved
        [1:0]        RW           muxctrl_reg2             00: GPIO0_2
                                                           01: EEE
                                                           10: FFF_OUT/GGG_CLK
                                                           11: reserved
muxctrl_reg22
                         muxctrl_reg22 is a multiplexing control register.
                          0x058                muxctrl_reg22               0x00000000
                                                                                 muxctrl_reg2
        [31:2]       RO           reserved                 Reserved
        [1:0]        RW           muxctrl_reg22            00: GPIO2_2
                                                           01: HHH
                                                           11: III
2.4 Software Multiplexed Pins
2.1.1 Package
                  HiBar V200 uses the lead thin & fine-pitch ball grid array
2.3 Pin Multiplexing Control Registers
muxctrl_reg0
                          0x000                muxctrl_reg0                0x00000000
        [1:0]        RW           muxctrl_reg0             00: GPIO0_0
                                                           01: JJJ
2.4 Software Multiplexed Pins
"""

SELFTEST_TABLE = '''
MUXCTRL(FOO_0, 0x10000000, "GPIO0_0", "AAA", "reserved", "BBB", "CCC")
MUXCTRL(FOO_1, 0x10000004, "GPIO0_1", "DDD")
MUXCTRL(FOO_2, 0x10000008, "GPIO0_2", "EEE", "FFF_OUT/GGG_CLK")
MUXCTRL(FOO_22, 0x10000058, "GPIO2_2", "HHH",
        "III")
'''


def selftest():
    """Run the parsing and the row verdicts against a built-in fixture.

    Data sheets cannot live in this repo, so --verify needs a document CI
    does not have. This needs nothing. Every case below is one that has
    silently produced a wrong answer at least once.
    """
    fails = []

    def check(ok, what):
        if not ok:
            fails.append(what)

    lines = SELFTEST_DOC.splitlines()

    # 1. both SoCs' chapters are found, and named from the package blurb
    ch = chapters(lines)
    check([c[0] for c in ch] == ["HiFoo V100", "HiBar V200"],
          "chapters: %r" % ([c[0] for c in ch],))
    if not ch:
        print("gen: fixture has no chapter at all", file=sys.stderr)
        return 1

    ds = registers(lines, ch[0][1], ch[0][2])

    # 2. a value list broken across a page break is still one list, and an
    #    unassigned value in the middle becomes a hole that shifts the rest
    check(ds.get(0) == ["GPIO0_0", "AAA", "reserved", "BBB", "CCC"],
          "reg0 (page break + middle hole): %r" % (ds.get(0),))

    # 3. a single-bit field. Insisting on three binary digits silently drops
    #    every 1- and 2-bit register in the document.
    check(ds.get(1) == ["GPIO0_1", "DDD"], "reg1 (1-bit): %r" % (ds.get(1),))

    # 4. a slashed name is one name, and a spelled-out "11: reserved" is a
    #    hole rather than a fifth entry
    check(ds.get(2) == ["GPIO0_2", "EEE", "FFF_OUT/GGG_CLK"],
          "reg2 (slash, trailing reserved): %r" % (ds.get(2),))

    # 5. THE ONE THAT BIT. reg22's bit diagram repeats its name indented, and
    #    pdftotext clips the trailing digit so the line reads "muxctrl_reg2".
    #    Keyed on, it hands reg22's values to reg2 twenty registers earlier.
    check(ds.get(22) == ["GPIO2_2", "HHH", "reserved", "III"],
          "reg22 (clipped diagram label): %r" % (ds.get(22),))
    check("HHH" not in (ds.get(2) or []),
          "reg2 was contaminated by reg22's diagram label")

    # 6. the second chapter is a different SoC, not more of the first
    ds2 = registers(lines, ch[1][1], ch[1][2])
    check(ds2.get(0) == ["GPIO0_0", "JJJ"], "second chapter: %r" % (ds2.get(0),))

    # 7. a value too wide for its own field means the parse is wrong, and
    #    saying so beats publishing it
    try:
        registers(["muxctrl_reg9",
                   "        [0]          RW           muxctrl_reg9   0: X",
                   "                                                11: Y"], 0, 3)
        check(False, "a value outside its field width was accepted")
    except SystemExit:
        pass

    # 8. the table parse, including a row wrapped across lines
    have = table_rows(SELFTEST_TABLE, "FOO_")
    check(sorted(have) == [0x10000000, 0x10000004, 0x10000008, 0x10000058],
          "table_rows addresses: %r" % (sorted(map(hex, have)),))
    check(have.get(0x10000058, (None, None, []))[2] == ["GPIO2_2", "HHH", "III"],
          "table_rows wrapped row: %r" % (have.get(0x10000058),))

    # 9. the verdicts, which decide what --fix may touch
    base = 0x10000000
    cases = [
        (0x10000000, ["GPIO0_0", "AAA", "reserved", "BBB", "CCC"], OK),
        (0x10000000, ["GPIO0_0", "AAA", "BBB", "CCC"], HOLE),
        (0x10000000, ["GPIO0_0", "AAA", "reserved", "BBB", "ZZZ"], RENAME),
        (0x10000011, ["GPIO0_0"], UNALIGNED),   # not base + 4n
        (0x0FFFFFFC, ["GPIO0_0"], UNALIGNED),   # below base
        (0x10000100, ["GPIO0_0"], NOSUCH),      # no such register
    ]
    for addr, funcs, want in cases:
        got = classify(addr, funcs, ds, base)[0]
        check(got == want, "classify(%#x, %r) = %s, wanted %s"
              % (addr, funcs, got, want))

    for f in fails:
        print("check_hisi_padmux selftest: %s" % f, file=sys.stderr)
    if not fails:
        print("check_hisi_padmux: selftest passed")
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--selftest", action="store_true",
                    help="check the parsing against a built-in fixture and "
                         "exit; needs no data sheet, so CI can run it")
    ap.add_argument("--datasheet", required=True)
    ap.add_argument("--prefix", required=True,
                    help="MUXCTRL row prefix, e.g. EV20X_")
    ap.add_argument("--base", required=True,
                    help="physical address of muxctrl_reg0, e.g. 0x200f0000")
    ap.add_argument("--soc", help="which chapter, when the document has several")
    ap.add_argument("--reginfo", default="src/reginfo.c")
    ap.add_argument("--fix", action="store_true",
                    help="rewrite the mismatched rows in place")
    # --selftest stands alone: it parses a fixture, not a document
    if "--selftest" in sys.argv[1:]:
        return selftest()

    args = ap.parse_args()

    lines = as_text(args.datasheet).splitlines()
    found = chapters(lines)
    if not found:
        sys.exit("no '2.3 Pin Multiplexing Control Registers' chapter found")
    if args.soc:
        found = [c for c in found if args.soc.lower() in c[0].lower()]
        if not found:
            sys.exit("no chapter for %r" % args.soc)
    if len(found) > 1:
        print("this document covers several SoCs; pass --soc:", file=sys.stderr)
        for who, lo, hi in found:
            print("  %-16s (lines %d..%d)" % (who, lo, hi), file=sys.stderr)
        return 1

    who, lo, hi = found[0]
    ds = registers(lines, lo, hi)
    base = int(args.base, 16)
    src = open(args.reginfo, encoding="utf-8").read()
    have = table_rows(src, args.prefix)
    if not have:
        sys.exit("no MUXCTRL rows with prefix %r" % args.prefix)

    print("%s: %d registers in the data sheet, %d rows named %s*"
          % (who, len(ds), len(have), args.prefix))

    # A missing hole is the one thing this tool knows how to repair. Anything
    # else -- a row the data sheet does not describe, an address that is not
    # base + 4n, a function whose NAME differs, a register with no row at all
    # -- is kept apart, because each of those is at least as likely to mean
    # the wrong document, the wrong --soc or the wrong --base as a wrong
    # table, and "repairing" a table against the wrong document would wreck it.
    holes, unfixable, fixed = 0, 0, 0
    numbered = {}
    for addr in sorted(have):
        name, addr_s, funcs, whole = have[addr]
        verdict, num, want = classify(addr, funcs, ds, base)
        if num is not None:
            numbered[num] = name
        if verdict == UNALIGNED:
            print("  %-14s %s is not %s + 4n. The table's address is what gets"
                  " read and written, so this is not a naming quibble."
                  % (name, addr_s, args.base))
            unfixable += 1
            continue
        if verdict == NOSUCH:
            print("  %-14s %s -> muxctrl_reg%d is not in this data sheet"
                  % (name, addr_s, num))
            unfixable += 1
            continue
        if verdict == OK:
            continue
        print("  %-14s %s muxctrl_reg%d" % (name, addr_s, num))
        print("      table     : %s" % " ".join(funcs))
        print("      data sheet: %s" % " ".join(want))
        if verdict == RENAME:
            print("      ^ not just a missing hole; --fix will not touch this")
            unfixable += 1
            continue
        holes += 1
        if args.fix:
            new = "MUXCTRL(%s, %s, %s)" % (
                name, addr_s, ", ".join('"%s"' % f for f in want))
            assert src.count(whole) == 1, name
            src = src.replace(whole, new)
            fixed += 1

    missing = sorted(set(ds) - set(numbered))
    if missing:
        # a pad with no row is absent from every lookup, which is a quieter
        # wrong answer than a shifted selector, not a smaller one
        print("  in the data sheet but not in the table: %s"
              % ", ".join("muxctrl_reg%d" % n for n in missing))
        unfixable += len(missing)

    if args.fix and fixed and unfixable == 0:
        open(args.reginfo, "w", encoding="utf-8").write(src)
        print("rewrote %d rows -- now run scripts/format-changed" % fixed)
    elif args.fix and unfixable:
        print("NOT rewriting anything: %d problem(s) above are not missing "
              "holes. Check --soc, --base and the document before assuming "
              "the table is what is wrong." % unfixable)

    print("%d of %d rows disagree (%d missing holes, %d this tool will not "
          "touch)" % (holes + unfixable, len(have), holes, unfixable))
    if unfixable:
        return 1
    return 0 if holes == 0 or (args.fix and fixed == holes) else 1


if __name__ == "__main__":
    sys.exit(main())
