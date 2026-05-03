#!/usr/bin/env python3
"""Parse `ipctool trace` output, drop noise, segment into phases.

Reads the raw mixed log (ptrace pseudocode + interleaved streamer logs +
ptrace bookkeeping). Emits a JSON file describing phase boundaries and a
per-phase list of register operations.

Phases identified, in order on a clean Majestic capture:
  probe      bus scan, lots of failed reads, before any successful write
  mipi_vi    HiSilicon /dev/hi_mipi + /dev/vi pretty-printed structs
  init       sensor reset (0x100=0) ... stream-on (0x100=1)
  post_init  AE/exposure registers right after stream-on, before frame loop
  runtime    cyclic per-frame writes (same regs repeated > THRESHOLD times)

Usage:
    trace_segment.py <input.log> [--out segments.json]
"""
import argparse
import json
import re
import sys
from collections import Counter, defaultdict


# Pseudocode line patterns produced by src/ptrace.c
RE_WRITE = re.compile(r"^sensor_write_register\(0x([0-9a-fA-F]+),\s*0x([0-9a-fA-F]+)\);")
RE_READ = re.compile(
    r"^sensor_read_register\(0x([0-9a-fA-F]+)\);\s*/\*\s*->\s*0x([0-9a-fA-F]+)\s*\*/"
)
RE_READ_ERR = re.compile(r"^sensor_read_register\(0x([0-9a-fA-F]+)\);\s*/\*\s*\[err\]")
RE_ADDR = re.compile(r"^sensor_i2c_change_addr\(0x([0-9a-fA-F]+)\);?")
RE_USLEEP = re.compile(r"^usleep\((\d+)\)")
RE_BANNER = re.compile(r"^=+\s*([a-zA-Z0-9_\-]+)\s*=+")
RE_STRUCT_OPEN = re.compile(r"^[\w]+\s*=\s*\{$|^\.\w+\s*=\s*\{$")

# Lines we drop on sight (noise, not register ops)
RE_NOISE = re.compile(
    r"^("
    r"\s*$"  # blank
    r"|\d{2}:\d{2}:\d{2}\s+(DEBUG|INFO|WARN|ERROR)"  # majestic logs
    r"|error copy_from_process"
    r"|Cloned \d+ fds"
    r"|\[\d+\] child \d+ created"
    r"|parent \d+ created child \d+"
    r"|child \d+ exited"
    r"|child \d+ killed"
    r"|open\("
    r"|close\("
    r"|read\(\d+,"
    r"|write\(\d+,"
    r"|syscall\d+\("
    r"|\[err\]"
    r")"
)

# A register that gets written N+ times in the runtime tail is treated as
# part of the AE/AGC loop, not the init.
RUNTIME_REPEAT_THRESHOLD = 3


def parse(line):
    """Classify one line. Return (kind, payload) or None if dropped."""
    s = line.rstrip("\n")
    if RE_NOISE.match(s):
        return None

    m = RE_WRITE.match(s)
    if m:
        return ("write", {"reg": int(m.group(1), 16), "val": int(m.group(2), 16)})

    m = RE_READ.match(s)
    if m:
        return ("read", {"reg": int(m.group(1), 16), "val": int(m.group(2), 16)})
    m = RE_READ_ERR.match(s)
    if m:
        return ("read_err", {"reg": int(m.group(1), 16)})

    m = RE_ADDR.match(s)
    if m:
        return ("addr", {"addr": int(m.group(1), 16)})

    m = RE_USLEEP.match(s)
    if m:
        return ("sleep", {"us": int(m.group(1))})

    m = RE_BANNER.match(s)
    if m:
        return ("banner", {"dev": m.group(1)})

    if RE_STRUCT_OPEN.match(s):
        return ("struct_begin", {"head": s})
    if s.strip() == "};":
        return ("struct_end", {})

    # Anything left is preserved as opaque text (mipi/vi struct bodies, ioctl).
    return ("text", {"raw": s})


def collapse_struct(events):
    """Merge struct_begin..struct_end runs into a single struct event."""
    out = []
    i = 0
    while i < len(events):
        kind, payload = events[i]
        if kind == "struct_begin":
            buf = [payload["head"]]
            j = i + 1
            while j < len(events) and events[j][0] != "struct_end":
                if events[j][0] == "text":
                    buf.append(events[j][1]["raw"])
                j += 1
            buf.append("};")
            out.append(("struct", {"text": "\n".join(buf)}))
            i = j + 1
        else:
            out.append((kind, payload))
            i += 1
    return out


def find_init_bounds(events):
    """Return (init_start_idx, init_end_idx).

    Heuristic:
      - init_start = first 'write' whose reg == 0x100 and val == 0 (reset).
      - init_end = first subsequent 'write' whose reg == 0x100 and val == 1
                    (stream-on).
    """
    start = None
    for i, (k, p) in enumerate(events):
        if k == "write" and p["reg"] == 0x100 and p["val"] == 0:
            start = i
            break
    if start is None:
        return (None, None)
    for j in range(start + 1, len(events)):
        k, p = events[j]
        if k == "write" and p["reg"] == 0x100 and p["val"] == 1:
            return (start, j)
    return (start, len(events) - 1)


def find_runtime_start(events, init_end):
    """First index after init_end where cyclic per-frame writes start.

    Walk forward from init_end. Track recent write addrs in a window;
    when an addr repeats >= RUNTIME_REPEAT_THRESHOLD times in the post-init
    tail, mark the start of runtime as the FIRST occurrence of any such addr.
    """
    if init_end is None:
        return None
    write_addrs = []
    for k, p in events[init_end + 1 :]:
        if k == "write":
            write_addrs.append(p["reg"])
    repeat_addrs = {
        a for a, c in Counter(write_addrs).items() if c >= RUNTIME_REPEAT_THRESHOLD
    }
    if not repeat_addrs:
        return None
    for j in range(init_end + 1, len(events)):
        k, p = events[j]
        if k == "write" and p["reg"] in repeat_addrs:
            return j
    return None


def slice_events(events, start, end):
    return events[start : end + 1] if start is not None and end is not None else []


def serialize(events):
    """Convert internal event tuples to JSON-serializable dicts."""
    out = []
    for k, p in events:
        d = {"kind": k}
        d.update(p)
        out.append(d)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("--out", default=None, help="output JSON path")
    args = ap.parse_args()

    with open(args.input, errors="replace") as f:
        events = [e for e in (parse(line) for line in f) if e is not None]
    events = collapse_struct(events)

    # Phase boundaries.
    init_s, init_e = find_init_bounds(events)
    runtime_s = find_runtime_start(events, init_e)

    phases = {}
    if init_s is None:
        phases["pre_sensor"] = serialize(events)
    else:
        phases["pre_sensor"] = serialize(events[:init_s])
        phases["init"] = serialize(events[init_s : init_e + 1])
        if runtime_s is not None:
            phases["post_init"] = serialize(events[init_e + 1 : runtime_s])
            phases["runtime"] = serialize(events[runtime_s:])
        else:
            phases["post_init"] = serialize(events[init_e + 1 :])

    summary = {phase: len(events) for phase, events in phases.items()}
    out_path = args.out or args.input + ".segments.json"
    with open(out_path, "w") as f:
        json.dump({"summary": summary, "phases": phases}, f, indent=2)

    print(f"wrote {out_path}", file=sys.stderr)
    for phase, count in summary.items():
        print(f"  {phase:12s} {count} events", file=sys.stderr)


if __name__ == "__main__":
    main()
