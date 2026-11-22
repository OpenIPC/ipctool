#!/usr/bin/env python3
"""Turn Anyka vendor kernels' pin tables into src/hal/anyka_padmux.h.

The AK3918AV100 family (AV100, AV100X, EV300L) selects a pad's function with
a three-bit field in one of the SYSCTRL share-pin registers at 0x08000178 +
4 * reg_no, at a bit offset of its own. The vendor kernel's pinctrl driver
(drivers/pinctrl/anyka/ak_3918av100_pinctrl.c) lists, per pad,

    /* PIN funcmux_reg_no funcmux_reg_offset gpio_func_no
     * pad_drv_offset pad_ie_offset pupd_en_offset pupd_sel_offset
     * pad_sl_offset func_mux_max gpio_cfg */
    {16, 1, 21, 0, 0, 16, 16, 16, 16, 4, AK_PIN_CFG_ALL_ENABLE},

and the device-tree binding header (include/dt-bindings/pinctrl/<soc>_pinctrl.h)
names each selector value:

    #define XGPIO_016_FUNC_PWM0 2

This script reads the first five and the tenth columns of the one, and every
XGPIO_nnn_FUNC_name of the other. The driver picks its rows with
#ifdef CONFIG_MACH_<SOC>, so --table names the machine to evaluate them for.

Usage:
    gen_anyka_padmux.py \\
        --table AV100=AK3918AV100:<driver.c>:<ak3918av100_pinctrl.h> \\
        --table EV300L=AK3918EV300L:<driver.c>:<ak3918ev300l_pinctrl.h> \\
        > src/hal/anyka_padmux.h
    gen_anyka_padmux.py ... --verify src/hal/anyka_padmux.h

The script refuses rather than guesses: a selector the driver says the pad
does not have (>= func_mux_max), one wider than the field, a pad whose GPIO
function is not GPIO<pad> at the driver's gpio_func_no, a pad given the same
selector twice, a function for a pad the driver has no row for, or a
preprocessor line it does not understand all exit non-zero.
"""

import argparse
import hashlib
import re
import sys

SEL_BITS = 3
PIN_CON0 = 0x08000178
# Pads the driver drives from the power-management block, not the share-pin
# registers or the GPIO controller (pin_pmc()).
PMC_PADS = {80, 81}

ROW = re.compile(r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,"
                 r"\s*-?\d+\s*,\s*-?\d+\s*,\s*-?\d+\s*,\s*-?\d+\s*,"
                 r"\s*-?\d+\s*,\s*(\w+)\s*,")
FUNC = re.compile(r"^\s*#define\s+XGPIO_(\d+)_FUNC_(\w+)\s+(\d+)\s*$", re.M)


class Refusal(Exception):
    pass


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def pmx(name):
    """The PMX_ identifier for a name, mangled as tools/gen_padmux_names.py
    does."""
    ident = re.sub(r"[^A-Za-z0-9_]", "__", name)
    if not ident or ident[0].isdigit():
        ident = "_" + ident
    return "PMX_" + ident


def preprocess(text, mach, path):
    """The lines of `text` that CONFIG_MACH_<mach> compiles, for the forms the
    driver uses: #ifdef X, #if defined(X), #elif defined(X), #else, #endif.
    Anything else in a conditional is refused."""
    defined = {"CONFIG_MACH_" + mach}
    out, stack = [], []  # stack of (taking, taken_any)

    def active():
        return all(t for t, _ in stack)

    for line in text.split("\n"):
        s = line.strip()
        m = (re.fullmatch(r"#\s*ifdef\s+(\w+)", s)
             or re.fullmatch(r"#\s*if\s+defined\s*\(\s*(\w+)\s*\)", s))
        if m:
            take = m.group(1) in defined
            stack.append((take, take))
            continue
        m = re.fullmatch(r"#\s*elif\s+defined\s*\(\s*(\w+)\s*\)", s)
        if m:
            if not stack:
                raise Refusal("%s: #elif without #if" % path)
            _, any_ = stack.pop()
            take = not any_ and m.group(1) in defined
            stack.append((take, any_ or take))
            continue
        if re.fullmatch(r"#\s*else", s):
            if not stack:
                raise Refusal("%s: #else without #if" % path)
            _, any_ = stack.pop()
            stack.append((not any_, True))
            continue
        if re.fullmatch(r"#\s*endif.*", s):
            if not stack:
                raise Refusal("%s: #endif without #if" % path)
            stack.pop()
            continue
        if re.match(r"#\s*(if|ifndef|elif)\b", s):
            raise Refusal("%s: cannot evaluate %r" % (path, s))
        if active():
            out.append(line)
    if stack:
        raise Refusal("%s: unterminated #if" % path)
    return "\n".join(out)


def parse_driver(path, mach):
    with open(path, encoding="utf-8", errors="replace") as handle:
        text = strip_comments(handle.read())
    m = re.search(r"struct\s+gpio_sharepin\s+ak_sharepin\s*\[[^\]]*\]\s*=\s*\{",
                  text)
    if not m:
        raise Refusal("%s: no ak_sharepin table" % path)
    end = text.find("\n};", m.end())
    body = preprocess(text[m.end():end], mach, path)
    pads = {}
    for pin, reg, off, gsel, fmax in ROW.findall(body):
        pin, reg, off, gsel = int(pin), int(reg), int(off), int(gsel)
        if pin in pads:
            raise Refusal("%s: pad %d has two rows for %s" % (path, pin, mach))
        if fmax == "AK_GPIO_CONF_INVALID" or pin in PMC_PADS:
            pads[pin] = None
            continue
        fmax = int(fmax)
        if fmax > 1 and off + SEL_BITS > 32:
            raise Refusal("%s: pad %d's field at bit %d runs past the "
                          "register" % (path, pin, off))
        pads[pin] = (reg, off, gsel, fmax)
    if not pads:
        raise Refusal("%s: no rows for %s" % (path, mach))
    return pads


def parse_names(path):
    with open(path, encoding="utf-8", errors="replace") as handle:
        text = strip_comments(handle.read())
    names = {}
    for pin, name, sel in FUNC.findall(text):
        names.setdefault(int(pin), []).append((int(sel), name))
    if not names:
        raise Refusal("%s: no XGPIO_nnn_FUNC_ defines" % path)
    return names


def build(driver, header, mach):
    pads = parse_driver(driver, mach)
    names = parse_names(header)
    rows = []
    for pin in sorted(names):
        if pin not in pads:
            raise Refusal("%s: XGPIO_%03d has functions, but %s has no row "
                          "for it" % (header, pin, driver))
        conf = pads[pin]
        if conf is None:
            continue  # this machine does not have the pad, or no mux
        reg, off, gsel, fmax = conf
        seen, gpio = {}, None
        for sel, name in sorted(names[pin]):
            if sel >= fmax or sel >= 1 << SEL_BITS:
                raise Refusal("%s: XGPIO_%03d_FUNC_%s is selector %d; the "
                              "driver gives the pad %d" % (header, pin, name,
                                                           sel, fmax))
            if sel in seen:
                raise Refusal("%s: pad %d selector %d is both %s and %s"
                              % (header, pin, sel, seen[sel], name))
            seen[sel] = name
            if re.fullmatch(r"GPIO\d+", name):
                if name != "GPIO%d" % pin or sel != gsel:
                    raise Refusal("%s: pad %d's GPIO is %s at %d; the driver "
                                  "says GPIO%d at %d" % (header, pin, name,
                                                         sel, pin, gsel))
                gpio = sel
        if gpio is None:
            raise Refusal("%s: pad %d has no GPIO function" % (header, pin))
        # func_mux_max of 1 is a pad with GPIO alone and no field at all
        # (the driver writes nothing for it: GPIO37, GPIO50).
        fixed = fmax <= 1
        for sel in sorted(seen):
            rows.append((pin, 0xFF if fixed else reg, 0 if fixed else off,
                         sel, seen[sel]))
    return rows


def digest(path):
    with open(path, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()[:16]


def emit(tables, sources):
    out = []
    w = out.append
    w("/* Generated by tools/gen_anyka_padmux.py -- do not edit.")
    w(" *")
    w(" * Anyka AK3918AV100-family pad functions, one row per (pad, selector),")
    w(" * from the vendor kernel's pinctrl driver and device-tree binding")
    w(" * header. A pad's field is three bits at `shift` in the share-pin")
    w(" * register 0x%08X + 4 * reg; reg 0xFF is a pad with GPIO alone and"
      % PIN_CON0)
    w(" * no field. See src/hal/anyka_padmux.c.")
    w(" *")
    w(" * Regenerate with:")
    w(" *   tools/gen_anyka_padmux.py \\")
    w(" *       " + " \\\n *       ".join(
        "--table %s=%s:%s:%s" % (tag, mach, drv, hdr)
        for tag, mach, drv, hdr in sources))
    w(" *")
    w(" * From OpenIPC/linux, as <branch>@<commit>/<path>:")
    done = set()
    for _, _, drv, hdr in sources:
        for p in (drv, hdr):
            if p not in done:
                done.add(p)
                w(" *   %s  %s" % (digest(p), p))
    w(" */")
    w("")
    w("#ifndef HAL_ANYKA_PADMUX_H")
    w("#define HAL_ANYKA_PADMUX_H")
    w("")
    w('#include "hal/anyka_padmux_types.h"')
    for tag, rows in tables:
        pads = len({r[0] for r in rows})
        w("")
        w("/* %s: %d pads, %d functions. */" % (tag, pads, len(rows)))
        w("static const ak_func_t AK_%s_funcs[] = {" % tag)
        for pin, reg, off, sel, name in rows:
            w("    {%d, %s, %d, %d, %s}," % (
                pin, "0xFF" if reg == 0xFF else str(reg), off, sel, pmx(name)))
        w("};")
    w("")
    w("#endif /* HAL_ANYKA_PADMUX_H */")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--table", action="append", required=True,
                    metavar="TAG=MACH:DRIVER:HEADER")
    ap.add_argument("--verify", metavar="HEADER",
                    help="exit 1 if HEADER is not what this would write")
    opts = ap.parse_args()

    tables, sources = [], []
    try:
        for spec in opts.table:
            tag, _, rest = spec.partition("=")
            parts = rest.split(":")
            if not re.fullmatch(r"[A-Z0-9_]+", tag) or len(parts) != 3:
                raise Refusal("--table %r: want TAG=MACH:DRIVER:HEADER" % spec)
            mach, drv, hdr = parts
            tables.append((tag, build(drv, hdr, mach)))
            sources.append((tag, mach, drv, hdr))
    except (Refusal, OSError) as err:
        print("gen_anyka_padmux: %s" % err, file=sys.stderr)
        return 2

    text = emit(tables, sources)
    if opts.verify:
        with open(opts.verify, encoding="utf-8") as handle:
            if handle.read() != text:
                print("gen_anyka_padmux: %s is stale" % opts.verify,
                      file=sys.stderr)
                return 1
        return 0
    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
