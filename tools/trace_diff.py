#!/usr/bin/env python3
"""Diff a generated init register sequence against a reference C source.

Extracts every `sensor_write_register(0xADDR, 0xVAL)` call from each file,
then compares as ordered sequences of (reg, val) pairs. Reports:
  * unique addresses in each
  * intersection / asymmetric differences
  * value mismatches at common addresses (last value wins per side)
  * order similarity (longest common subsequence ratio)

Usage:
    trace_diff.py <generated.c> <reference.c>
"""
import argparse
import re
import sys


# Matches both `sensor_write_register(0xABCD, 0xEF)` and
# `sc2315e_write_register(ViPipe, 0xABCD, 0xEF)`.  Tolerant of optional
# 0x prefix on either operand and decimal literals (reference uses `1`
# rather than `0x01` in many places).
RE_WRITE = re.compile(
    r"\bsensor_write_register\s*\(\s*"  # function name (matches *_write_register too via lookbehind below)
    r"(?:[^,]+,\s*)?"                   # optional ViPipe / first arg
    r"((?:0[xX])?[0-9a-fA-F]+)\s*,\s*"
    r"((?:0[xX])?[0-9a-fA-F]+)\s*\)"
)
# Wider net for things like sc2315e_write_register(...).
RE_ANY_WRITE = re.compile(
    r"\b\w*write_register\s*\(\s*"
    r"(?:[^,]+,\s*)?"
    r"((?:0[xX])?[0-9a-fA-F]+)\s*,\s*"
    r"((?:0[xX])?[0-9a-fA-F]+)\s*\)"
)


def parse_int(s):
    s = s.strip()
    if s.lower().startswith("0x"):
        return int(s, 16)
    # Reference uses bare 0..15 in some lines; treat those as decimal.
    return int(s, 10) if not any(c in "abcdefABCDEF" for c in s) else int(s, 16)


def extract_function_body(src, fn_name):
    """Return text inside `void <fn_name>(...) { ... }`, brace-balanced.

    Strings/comments are not handled — fine for these driver files which
    keep everything on one line per write.
    """
    pat = re.compile(r"\bvoid\s+" + re.escape(fn_name) + r"\s*\([^)]*\)\s*\{")
    m = pat.search(src)
    if not m:
        return None
    start = m.end()
    depth = 1
    i = start
    while i < len(src) and depth:
        c = src[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return src[start:i]
        i += 1
    return src[start:]


def extract_writes(path, scope=None):
    with open(path) as f:
        src = f.read()
    if scope:
        body = extract_function_body(src, scope)
        if body is None:
            print(f"WARNING: function {scope!r} not found in {path}", file=sys.stderr)
            return []
        src = body
    writes = []
    for m in RE_ANY_WRITE.finditer(src):
        try:
            reg = parse_int(m.group(1))
            val = parse_int(m.group(2))
        except ValueError:
            continue
        if reg > 0xFFFF or val > 0xFFFF:
            continue
        writes.append((reg, val))
    return writes


def lcs_ratio(a, b):
    """Length of LCS / max(len). Cheap O(n*m) DP, fine for ~200x200."""
    n, m = len(a), len(b)
    if n == 0 or m == 0:
        return 0.0
    prev = [0] * (m + 1)
    for i in range(1, n + 1):
        cur = [0] * (m + 1)
        for j in range(1, m + 1):
            if a[i - 1] == b[j - 1]:
                cur[j] = prev[j - 1] + 1
            else:
                cur[j] = max(cur[j - 1], prev[j])
        prev = cur
    return prev[m] / max(n, m)


def report(gen_path, ref_path, gen_scope=None, ref_scope=None):
    gen = extract_writes(gen_path, gen_scope)
    ref = extract_writes(ref_path, ref_scope)

    gen_regs = {r for r, _ in gen}
    ref_regs = {r for r, _ in ref}

    # Per-register *last* value seen (matches "init complete" state).
    gen_last = {}
    for r, v in gen:
        gen_last[r] = v
    ref_last = {}
    for r, v in ref:
        ref_last[r] = v

    common = gen_regs & ref_regs
    only_gen = gen_regs - ref_regs
    only_ref = ref_regs - gen_regs

    val_match = sum(1 for r in common if gen_last[r] == ref_last[r])
    val_mismatch = [
        (r, gen_last[r], ref_last[r]) for r in common if gen_last[r] != ref_last[r]
    ]

    # Sequence similarity (order-aware).
    seq_ratio = lcs_ratio(gen, ref)

    print(f"generated: {gen_path} ({len(gen)} writes, {len(gen_regs)} unique regs)")
    print(f"reference: {ref_path} ({len(ref)} writes, {len(ref_regs)} unique regs)")
    print()
    print(f"address match:  {len(common)} / {len(ref_regs)} ref regs present "
          f"({100*len(common)/max(1,len(ref_regs)):.1f}%)")
    print(f"value match:    {val_match} / {len(common)} matching addrs "
          f"({100*val_match/max(1,len(common)):.1f}%)")
    print(f"sequence (LCS): {seq_ratio*100:.1f}% of max(len)")
    print()

    if only_ref:
        print(f"-- regs in REF but NOT in GENERATED ({len(only_ref)}) --")
        for r in sorted(only_ref):
            print(f"  0x{r:04X} = 0x{ref_last[r]:02X}")
        print()
    if only_gen:
        print(f"-- regs in GENERATED but NOT in REF ({len(only_gen)}) --")
        for r in sorted(only_gen):
            print(f"  0x{r:04X} = 0x{gen_last[r]:02X}")
        print()
    if val_mismatch:
        print(f"-- value mismatches at {len(val_mismatch)} common regs --")
        for r, gv, rv in sorted(val_mismatch):
            print(f"  0x{r:04X}  gen=0x{gv:02X}  ref=0x{rv:02X}")
        print()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("generated")
    ap.add_argument("reference")
    ap.add_argument("--gen-scope", help="restrict to this function body in generated")
    ap.add_argument("--ref-scope", help="restrict to this function body in reference")
    args = ap.parse_args()
    report(args.generated, args.reference, args.gen_scope, args.ref_scope)


if __name__ == "__main__":
    main()
