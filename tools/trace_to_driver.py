#!/usr/bin/env python3
"""Generate a HiSilicon-SDK-shaped sensor init function from a segmented trace.

Reads the JSON produced by trace_segment.py and emits a C source file that
mirrors the structure of widgetii/smart_sc2315e/sc2315e_sensor_ctl.c.

Usage:
    trace_to_driver.py <segments.json> --sensor sc2315e [--out file.c]
"""
import argparse
import json
import re
import sys


HEADER = """\
/*
 * AUTO-GENERATED from ipctool trace via tools/trace_to_driver.py
 * Source: {src}
 * Sensor: {sensor}
 *
 * This is a scaffold. Compare against a known-good driver before use.
 * Runtime AE/AGC writes are emitted as a comment block, not C code -
 * the surrounding logic (gain math, exposure scaling) is not derivable
 * from a register trace alone.
 *
 * To integrate into a HiSilicon SDK build:
 *   - Replace the SDK-stubs block with #include "hi_comm_video.h" and
 *     #include "hi_sns_ctrl.h"
 *   - Replace the sensor_write_register stub with the vendor's bus-aware
 *     implementation, typically declared in <sensor>_sensor_ctl.c
 *
 * As shipped, this file passes `gcc -fsyntax-only` standalone.
 */
#include <unistd.h>

/* --- SDK stubs (delete when integrating into a vendor SDK) --- */
typedef int VI_PIPE;
static inline void sensor_write_register(unsigned int addr, unsigned int val)
{{
    (void)addr;
    (void)val;
}}
/* --- end SDK stubs --- */
"""

FN_TEMPLATE = """
void {sensor}_{suffix}(VI_PIPE ViPipe) {{
  (void)ViPipe;  /* implicit pipe ID in the vendor SDK; void-cast for the standalone scaffold */
{body}}}
"""


def fmt_write(reg, val, indent="  "):
    return f"{indent}sensor_write_register(0x{reg:04X}, 0x{val:02X});\n"


def fmt_sleep(us, indent="  "):
    if us == 0:
        # ipctool emits usleep(0) for very short sleeps; reference drivers
        # commonly use 10ms here. Flagged as TODO.
        return f"{indent}usleep(10000);  /* TODO: trace shows 0us, ref uses 10ms */\n"
    return f"{indent}usleep({us});\n"


def emit_phase(events, indent="  "):
    out = []
    for ev in events:
        if ev["kind"] == "write":
            out.append(fmt_write(ev["reg"], ev["val"], indent))
        elif ev["kind"] == "sleep":
            out.append(fmt_sleep(ev["us"], indent))
        elif ev["kind"] == "addr":
            out.append(f"{indent}/* sensor_i2c_change_addr(0x{ev['addr']:02X}) */\n")
        elif ev["kind"] == "banner":
            out.append(f"{indent}/* === bus: {ev['dev']} === */\n")
        elif ev["kind"] == "struct":
            # Structs are hoisted to file scope by emit_structs_block; skip
            # them here so they don't appear inline as comments.
            pass
        elif ev["kind"] in ("read", "read_err"):
            # Reads happen during probe and chip-id checks. Document but
            # do not emit as code (they don't belong in init).
            r = ev["reg"]
            v = ev.get("val")
            tag = "read_err" if ev["kind"] == "read_err" else "read"
            line = f"{indent}/* {tag} 0x{r:04X}"
            if v is not None:
                line += f" -> 0x{v:02X}"
            line += " */\n"
            out.append(line)
    return "".join(out)


# Parses `combo_dev_attr_t SENSOR_ATTR = {` -> ("combo_dev_attr_t", "SENSOR_ATTR")
RE_STRUCT_HEAD = re.compile(r"^(\w+)\s+(\w+)\s*=\s*\{$")


def collect_structs(phases):
    """Return list of (type_name, var_name, text) tuples, deduped by var_name.

    A streamer that issues multiple HI_MPI_*_SetDevAttr calls during init
    (e.g., per-pipe configuration) shows up as several struct events with
    the same var_name; the last value wins, mirroring the final state the
    sensor was configured into.
    """
    by_name = {}
    for events in phases.values():
        for ev in events:
            if ev["kind"] != "struct":
                continue
            head = ev["text"].splitlines()[0]
            m = RE_STRUCT_HEAD.match(head)
            if not m:
                continue
            type_name, var_name = m.group(1), m.group(2)
            by_name[var_name] = (type_name, var_name, ev["text"])
    return list(by_name.values())


def emit_structs_block(structs):
    """Emit MIPI/VI struct declarations wrapped in `#if 0` so the standalone
    build skips them (they reference vendor-only enum constants like
    INPUT_MODE_MIPI). User removes the guard when integrating into a
    HiSilicon SDK build, where the enums and types are in scope via
    hi_comm_video.h / hi_mipi.h.
    """
    if not structs:
        return ""
    lines = []
    lines.append("/*")
    lines.append(" * MIPI/VI device attributes captured from the running streamer's")
    lines.append(" * HI_MPI_*_SetDevAttr ioctls. Wrapped in #if 0 so the standalone")
    lines.append(" * scaffold builds without vendor headers; remove the #if 0/#endif")
    lines.append(" * when integrating into a HiSilicon SDK build (the enum constants")
    lines.append(" * and struct typedefs come from hi_comm_video.h / hi_mipi.h).")
    lines.append(" *")
    lines.append(" * Note: ipctool's V4 VI-dev dumper emits the variable name in")
    lines.append(" * `pstViDevAttr VI_DEV_ATTR_S` order; in real SDK code the type")
    lines.append(" * is VI_DEV_ATTR_S and the variable name is yours to choose.")
    lines.append(" */")
    lines.append("#if 0")
    for _, _, text in structs:
        lines.append(text)
        lines.append("")  # blank between declarations
    lines.append("#endif")
    return "\n".join(lines) + "\n"


def runtime_summary(events):
    """Per-register write count, last value, and first-seen index."""
    from collections import Counter

    counts = Counter()
    last_val = {}
    first_seen = {}  # trace-order, used to emit hot regs in original sequence
    for i, ev in enumerate(events):
        if ev["kind"] == "write":
            counts[ev["reg"]] += 1
            last_val[ev["reg"]] = ev["val"]
            first_seen.setdefault(ev["reg"], i)
    items = sorted(counts.items(), key=lambda kv: -kv[1])
    return [(reg, n, last_val[reg], first_seen[reg]) for reg, n in items]


def runtime_ae_skeleton(events, sensor, top_k=8, hot_ratio=0.25):
    """Emit a callback skeleton from the runtime phase.

    Selects the most-frequently-written runtime registers (top_k by count,
    each above hot_ratio * max_count) and emits a void function that
    writes the last-seen value to each. Order matches the trace.

    The values are placeholders; the math driving them - exposure scaling,
    gain LUTs, threshold-branched tuning - is not in the register trace.
    Reverse-engineer via the vendor's cmos_inttime_update /
    cmos_gains_update equivalents.
    """
    summary = runtime_summary(events)
    if not summary:
        return ""

    max_count = summary[0][1]
    threshold = max(1, int(max_count * hot_ratio))
    hot = [(r, n, v, idx) for r, n, v, idx in summary[:top_k] if n >= threshold]
    if not hot:
        return ""

    # Emit in trace-order so the writes look plausible to the AE loop.
    ordered = sorted(hot, key=lambda t: t[3])

    lines = []
    lines.append("/*")
    lines.append(f" * AE/AGC steady-state callback skeleton ({sensor}).")
    lines.append(" *")
    lines.append(" * Per-frame writes captured during steady state. Values shown are the")
    lines.append(" * last-seen values from the trace; the AE math driving them (gain LUTs,")
    lines.append(" * exposure scaling, threshold branches) is NOT derivable from a register")
    lines.append(" * trace alone. Cross-reference the vendor's cmos_inttime_update /")
    lines.append(" * cmos_gains_update equivalents to fill in the math.")
    lines.append(" *")
    lines.append(" * Hot register frequency:")
    lines.append(" *   reg      count    last value")
    for reg, count, val, _ in hot:
        lines.append(f" *   0x{reg:04X}   {count:5d}    0x{val:02X}")
    lines.append(" */")
    lines.append(f"void {sensor}_ae_step(VI_PIPE ViPipe)")
    lines.append("{")
    lines.append("    (void)ViPipe;")
    for reg, _, val, _ in ordered:
        lines.append(
            f"    sensor_write_register(0x{reg:04X}, 0x{val:02X});  /* TODO: derive */"
        )
    lines.append("}")
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("segments")
    ap.add_argument("--sensor", required=True)
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    with open(args.segments) as f:
        data = json.load(f)
    phases = data["phases"]

    parts = [HEADER.format(src=args.segments, sensor=args.sensor)]

    structs = collect_structs(phases)
    if structs:
        parts.append("\n")
        parts.append(emit_structs_block(structs))

    init = phases.get("init", [])
    post_init = phases.get("post_init", [])
    if init:
        parts.append(
            FN_TEMPLATE.format(
                sensor=args.sensor,
                suffix="linear_init",
                body=emit_phase(init, indent="  "),
            )
        )
    if post_init:
        # Kept separate from init: these are AE/exposure prime writes that
        # would otherwise overwrite init values when the diff merges them.
        parts.append(
            FN_TEMPLATE.format(
                sensor=args.sensor,
                suffix="post_init_exposure_prime",
                body=emit_phase(post_init, indent="  "),
            )
        )

    runtime = phases.get("runtime", [])
    if runtime:
        skeleton = runtime_ae_skeleton(runtime, args.sensor)
        if skeleton:
            parts.append("\n")
            parts.append(skeleton)

    src = "".join(parts)
    out_path = args.out or args.segments + ".c"
    with open(out_path, "w") as f:
        f.write(src)
    print(f"wrote {out_path}", file=sys.stderr)


if __name__ == "__main__":
    main()
