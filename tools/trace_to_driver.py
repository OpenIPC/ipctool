#!/usr/bin/env python3
"""Generate a HiSilicon-SDK-shaped sensor init function from a segmented trace.

Reads the JSON produced by trace_segment.py and emits a C source file that
mirrors the structure of widgetii/smart_sc2315e/sc2315e_sensor_ctl.c.

Usage:
    trace_to_driver.py <segments.json> --sensor sc2315e [--out file.c]
"""
import argparse
import json
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
            out.append(f"{indent}/*\n")
            for line in ev["text"].splitlines():
                out.append(f"{indent} * {line}\n")
            out.append(f"{indent} */\n")
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


def runtime_summary(events):
    """Per-register write count and last value seen."""
    from collections import Counter

    counts = Counter()
    last_val = {}
    for ev in events:
        if ev["kind"] == "write":
            counts[ev["reg"]] += 1
            last_val[ev["reg"]] = ev["val"]
    items = sorted(counts.items(), key=lambda kv: -kv[1])
    return [(reg, n, last_val[reg]) for reg, n in items]


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
        parts.append("\n/*\n")
        parts.append(" * RUNTIME AE/AGC HOT REGISTERS\n")
        parts.append(" * (per-frame writes during steady state - the math\n")
        parts.append(" *  that drives these values is NOT in the trace)\n")
        parts.append(" *\n")
        parts.append(" *   reg     count    last value\n")
        for reg, n, v in runtime_summary(runtime)[:32]:
            parts.append(f" *   0x{reg:04X}    {n:5d}    0x{v:02X}\n")
        parts.append(" */\n")

    src = "".join(parts)
    out_path = args.out or args.segments + ".c"
    with open(out_path, "w") as f:
        f.write(src)
    print(f"wrote {out_path}", file=sys.stderr)


if __name__ == "__main__":
    main()
