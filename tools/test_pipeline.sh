#!/usr/bin/env bash
# End-to-end smoke test for the post-processing pipeline.
#
# Synthesises a minimal trace, runs:
#   trace_segment.py -> trace_to_driver.py -> gcc -fsyntax-only
# and verifies each step succeeds. Designed for CI: no hardware needed,
# no external deps beyond python3 and gcc.

set -euo pipefail

cd "$(dirname "$0")/.."

tmp=$(mktemp -d)
trap "rm -rf $tmp" EXIT

# Minimal synthetic trace: a MIPI struct dump (pre_sensor) + init (reset
# + a few writes + stream-on) + runtime (one register written enough
# times to trigger the runtime heuristic at threshold >= 3).
cat > "$tmp/sample.log" <<'TRACE'
[100] child 101 created
combo_dev_attr_t SENSOR_ATTR = {
	.devno = 0,
	.input_mode = INPUT_MODE_MIPI,
	.lane_id = {0, 2, -1, -1},
};
sensor_i2c_change_addr(0x60);
sensor_write_register(0x100, 0x0);
usleep(10000)
sensor_write_register(0x3034, 0x81);
sensor_write_register(0x3039, 0xa6);
sensor_write_register(0x320e, 0x4);
sensor_write_register(0x100, 0x1);
sensor_write_register(0x3e02, 0x80);
sensor_write_register(0x100, 0x0);
sensor_write_register(0x320e, 0x8);
sensor_write_register(0x320c, 0x10);
sensor_write_register(0x100, 0x1);
sensor_write_register(0x5781, 0x60);
sensor_write_register(0x5781, 0x60);
sensor_write_register(0x5781, 0x60);
sensor_write_register(0x5781, 0x60);
TRACE

echo "== trace_segment.py =="
python3 tools/trace_segment.py "$tmp/sample.log" --out "$tmp/segments.json"
test -s "$tmp/segments.json" || { echo "segments.json empty"; exit 1; }
python3 - "$tmp/segments.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
phases = d["phases"]
assert phases.get("init"),    "init phase missing"
assert phases.get("runtime"), "runtime phase missing"
assert d["summary"]["init"]    >= 3, f"init too short: {d['summary']}"
assert d["summary"]["runtime"] >= 3, f"runtime too short: {d['summary']}"
print(f"  phases: {d['summary']}")
PY

echo "== trace_to_driver.py =="
python3 tools/trace_to_driver.py "$tmp/segments.json" \
    --sensor testsensor --out "$tmp/driver.c"
test -s "$tmp/driver.c" || { echo "driver.c empty"; exit 1; }
grep -q '^void testsensor_linear_init' "$tmp/driver.c" \
    || { echo "linear_init function not emitted"; exit 1; }
grep -q '^void testsensor_ae_step' "$tmp/driver.c" \
    || { echo "ae_step skeleton not emitted"; exit 1; }
grep -q '^void testsensor_set_mode_1' "$tmp/driver.c" \
    || { echo "set_mode_1 (mode-switch) function not emitted"; exit 1; }
grep -q '^combo_dev_attr_t SENSOR_ATTR' "$tmp/driver.c" \
    || { echo "MIPI struct not emitted at file scope"; exit 1; }
grep -q '^#if 0' "$tmp/driver.c" \
    || { echo "struct block not wrapped in #if 0"; exit 1; }

echo "== gcc -fsyntax-only =="
gcc -Wall -Wextra -fsyntax-only "$tmp/driver.c"

echo "== gcc -c (full compile) =="
gcc -Wall -Wextra -c "$tmp/driver.c" -o "$tmp/driver.o"
test -s "$tmp/driver.o" || { echo "object empty"; exit 1; }

echo "== trace_diff.py self-diff (must be 100%) =="
python3 tools/trace_diff.py "$tmp/driver.c" "$tmp/driver.c" \
    --gen-scope testsensor_linear_init \
    --ref-scope testsensor_linear_init | tee "$tmp/diff.out"
grep -q '100.0%)' "$tmp/diff.out" \
    || { echo "self-diff not 100%"; exit 1; }

# Diff against a reference that uses the older RE-port shape:
# `int <fn>(VI_PIPE)` returning int (not void), with
# `sensor_write_register_0(...)` (bus-numbered suffix). Validates that
# extract_function_body and RE_ANY_WRITE handle both styles.
echo "== trace_diff.py cross-style ref =="
cat > "$tmp/ref_old_style.c" <<'REF'
#include <unistd.h>
typedef int VI_PIPE;
static void sensor_write_register_0(unsigned int a, unsigned int v)
{ (void)a; (void)v; }

int oldstyle_init(VI_PIPE pipe) {
    (void)pipe;
    sensor_write_register_0(0x100, 0x0);
    sensor_write_register_0(0x3034, 0x81);
    sensor_write_register_0(0x3039, 0xa6);
    sensor_write_register_0(0x320e, 0x4);
    sensor_write_register_0(0x100, 0x1);
    return 0;
}
REF
python3 tools/trace_diff.py "$tmp/driver.c" "$tmp/ref_old_style.c" \
    --gen-scope testsensor_linear_init \
    --ref-scope oldstyle_init | tee "$tmp/cross.out"
grep -q 'address match:  4 / 4' "$tmp/cross.out" \
    || { echo "cross-style ref didn't match (relaxed regex broken?)"; exit 1; }

# Sony-IMX init pattern: 0x3000=1 starts standby, 0x3000=0 releases.
# Reverse polarity from SmartSens. Validates the per-family pattern table.
echo "== sony_imx pattern detection =="
cat > "$tmp/sony.log" <<'TRACE'
[200] child 201 created
sensor_i2c_change_addr(0x34);
sensor_write_register(0x3000, 0x1);
sensor_write_register(0x3005, 0x1);
sensor_write_register(0x3007, 0x0);
sensor_write_register(0x3009, 0x2);
sensor_write_register(0x3000, 0x0);
TRACE
python3 tools/trace_segment.py "$tmp/sony.log" --out "$tmp/sony.json" 2>&1
python3 - "$tmp/sony.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
assert d.get("init_pattern") == "sony_imx", \
    f"expected sony_imx, got {d.get('init_pattern')!r}"
assert d["summary"].get("init", 0) >= 3, \
    f"sony init too short: {d['summary']}"
print(f"  detected: {d['init_pattern']}, init={d['summary'].get('init')} events")
PY
python3 tools/trace_to_driver.py "$tmp/sony.json" \
    --sensor sonyimx --out "$tmp/sony.c"
gcc -Wall -Wextra -fsyntax-only "$tmp/sony.c"
grep -q '^void sonyimx_linear_init' "$tmp/sony.c" \
    || { echo "sony scaffold missing linear_init"; exit 1; }

echo "OK: pipeline test passed"
