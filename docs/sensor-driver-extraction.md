# Extracting sensor drivers from binary firmware with `ipctool trace`

A guide for researchers who need to recover an image-sensor init sequence
(and the surrounding ISP/MIPI setup) from a running but source-less
streamer — typically because the camera vendor has stopped shipping
updates and the only remaining trace of the driver lives inside an `.so`
or a statically-linked binary.

This document walks through the full pipeline end-to-end, using
SmartSens **SC2315E** on a Hisilicon **HI3516EV200** as the worked
example, captured separately from OpenIPC's [Majestic][majestic] streamer
and from XiongMai's proprietary Sofia. Both produce a register-by-register
identical init sequence — and one that matches the public OpenIPC
[smart_sc2315e][smart_sc2315e] reference source exactly.

[majestic]: https://github.com/openipc
[smart_sc2315e]: https://github.com/widgetii/smart_sc2315e

## What `ipctool trace` does

It is a [`ptrace(2)`][ptrace]-based syscall interceptor, similar in spirit
to `strace`, but specialised for camera I/O. It hooks `open`, `read`,
`write`, `ioctl`, and `nanosleep`, and decodes the device-specific
payloads on the fly:

- `/dev/i2c-*`, `/dev/hi_i2c` → `sensor_write_register(0x..., 0x..)` /
  `sensor_read_register(0x...) /* -> 0x.. */` / `sensor_i2c_change_addr(0x..)`
- `/dev/spidev*`, `/dev/ssp` → `ssp_write_register(...)` / SPI message dumps
- `/dev/hi_mipi`, `/dev/vi` → pretty-printed `combo_dev_attr_t SENSOR_ATTR = {...}`
- `/dev/xm_gpio`, `/sys/class/gpio/...` → GPIO request/direction/write events
- `/dev/mtd*` → MTD ioctl dumps

The output is a stream of C-pseudocode interleaved with bus-banner
markers (`========================== i2c-29 ==========================`).
ARM-only (the syscall-number table is hardcoded for ARM 32-bit EABI).

[ptrace]: https://man7.org/linux/man-pages/man2/ptrace.2.html

## Quick worked example

```console
# build  (downloads OpenIPC's CI toolchain, UPX-packs the binary)
$ tools/capture_sensor.sh build

# capture from a Majestic camera over ssh
$ tools/capture_sensor.sh majestic --host openipc-hi3516ev200.lan \
                                   --secs 35 --out tools/dumps/cap.log

# segment, generate, diff against a known-good vendor source
$ python3 tools/trace_segment.py    tools/dumps/cap.log
$ python3 tools/trace_to_driver.py  tools/dumps/cap.log.segments.json \
                                    --sensor sc2315e \
                                    --out tools/dumps/cap.c
$ python3 tools/trace_diff.py       tools/dumps/cap.c \
                                    /path/to/smart_sc2315e/sc2315e_sensor_ctl.c \
                                    --gen-scope sc2315e_linear_init \
                                    --ref-scope sc2315e_linear_1080P30_init

generated: tools/dumps/cap.c (172 writes, 169 unique regs)
reference: smart_sc2315e/sc2315e_sensor_ctl.c (172 writes, 169 unique regs)

address match:  169 / 169 ref regs present (100.0%)
value match:    169 / 169 matching addrs (100.0%)
sequence (LCS): 100.0% of max(len)
```

A 100/100/100 match confirms that what the binary writes to the sensor
is exactly what the published vendor driver writes. If you trust the
reference, the trace gives you a buildable scaffold.

## Pipeline overview

```
camera (ARM)                         host (x86)
─────────────                        ──────────
streamer                ┌─────────► trace_segment.py
   │                    │              │
   ▼                    │              ▼
ipctool trace ──► .log file ──► trace_to_driver.py ──► .c scaffold
                                       │                    │
                                       ▼                    ▼
                                  segments.json     trace_diff.py vs reference
```

Each stage is dependency-free Python (segment / generate / diff) and one
shell wrapper for the camera-side capture (`capture_sensor.sh`). Nothing
needs to be installed on the camera beyond the staged `ipctool` binary.

## Stage 1 — Build a portable `ipctool`

You need an ARM static binary that will run on whichever embedded kernel
the target camera ships. Cross-compile flags vary, but two non-obvious
constraints apply:

### 1. Use the OpenIPC CI toolchain

Generic Buildroot/musl ARM toolchains often produce binaries with
slightly different code generation than the canonical CI build, which
is what every released `ipctool` tested against. Stick with the toolchain
referenced in [`.github/workflows/release-arm32.yml`](../.github/workflows/release-arm32.yml):

```bash
wget -qO- \
  https://github.com/OpenIPC/firmware/releases/download/toolchain/toolchain.hisilicon-hi3516cv100.tgz \
  | tar xzf - -C /opt
```

Then build with **direct compiler override**, no toolchain file:

```bash
PATH=/opt/arm-openipc-linux-musleabi_sdk-buildroot/bin:$PATH \
  cmake -H. -Bbuild \
        -DCMAKE_C_COMPILER=arm-openipc-linux-musleabi-gcc \
        -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### 2. UPX-pack the binary

Some embedded kernels (notably **XiongMai HiLinux** based on Linux 4.9)
will refuse to load a regular musl-static ELF and the busybox shell
falls back to interpreting the file as a script:

```
/utils/ipctool: line 1: syntax error: unexpected word (expecting ")")
```

That cryptic message is the kernel returning `ENOEXEC` for the binary,
followed by `sh` trying to read the ELF magic as shell. UPX wraps the
binary in a tiny self-decompressing stub with a single `PT_LOAD`
segment and (as a side effect) sets the OS/ABI byte to `UNIX - GNU`.
Both the OpenIPC and XiongMai kernels accept that:

```bash
upx --best build/ipctool -o build/ipctool-upx
```

The released `ipctool` from the OpenIPC GitHub releases is UPX-packed
for exactly this reason. `tools/capture_sensor.sh build` does this
automatically.

## Stage 2 — Camera-side capture

`ipctool trace` forks, `ptrace(TRACEME)`s, and `execv`s the target
binary. All clones/forks are followed via `PTRACE_O_TRACECLONE`, so a
multi-threaded streamer is captured as one stream.

The `--output=PATH` flag (added in this branch) is important: without
it, the trace pseudocode shares the child's `stdout`, and verbose
streamers (Majestic at INFO level, Sofia with its `[31m...[0m`
ANSI-coloured logs) interleave their own log lines with ours, sometimes
truncating mid-token. With `--output=PATH`, the parent `freopen`s its
stdout to PATH after `fork`, so the child's `stdout` keeps pointing at
the original tty.

### Capturing from Majestic (OpenIPC)

Majestic is a "good citizen": no watchdog, no path-based supervisors,
restartable via `/etc/init.d/S95majestic`. Capture is straightforward:

```bash
# uses ssh, expects passwordless key-based access
tools/capture_sensor.sh majestic --host openipc-hi3516ev200.lan \
                                 --secs 35
```

What it does:

```
killall majestic                                  # stop the live stream
ipctool-upx trace --output=/utils/dumps/log \
                  /usr/bin/majestic &             # run a fresh one under trace
sleep 35                                          # init + a few seconds AE
killall ipctool-upx majestic                      # stop the trace
/etc/init.d/S95majestic start                     # restore camera service
```

Output is a clean trace with no log interleaving. About 3000 lines for
40 s of capture (init + a few hundred AE-loop iterations).

### Capturing from XiongMai Sofia (HiLinux)

This is harder for three independent reasons. Document each gotcha
separately so you can recognise them on adjacent firmware variants.

**(a) Sofia is a network client of `XmServices_Mgr`, not a peer.**
Sofia opens a local TCP socket to talk to `XmServices_Mgr` for system
services like `GetWritableDir`. If `XmServices_Mgr` is dead, Sofia
spins forever in `LibXmComm_NetTcp_recvPacket: timeout`, never reaching
sensor init. So the capture flow must keep `XmServices_Mgr` alive.

**(b) `XmServices_Mgr` is not a supervisor.** Despite the name, it
forks `SofiaRun.sh` exactly once at boot — from `/etc/init.d/rcS` — and
does not restart it if Sofia or `SofiaRun.sh` exits. Killing Sofia
leaves the camera with no running streamer until manual intervention.

**(c) The watchdog is fed by `XmServices_Mgr`, not Sofia.** Killing
Sofia by itself does not trigger a hardware reboot, so you have time.
But killing `XmServices_Mgr` will reboot the camera within ~30 s.

The working recipe is a **bind-mount wrapper**: insert ipctool between
`SofiaRun.sh` and `/usr/bin/Sofia` without disturbing `XmServices_Mgr`:

```bash
# 1. Stage a copy of the real Sofia under a path the wrapper can exec
cp /usr/bin/Sofia /utils/Sofia.real

# 2. Create a wrapper that runs Sofia under trace, redirecting trace
#    output to a file (Sofia's own stdout/stderr go to /dev/null as
#    they did under the real SofiaRun.sh anyway)
cat > /utils/sofia.wrap.sh <<'EOF'
#!/bin/sh
exec /utils/ipctool-upx trace --skip=usleep \
                              --output=/utils/dumps/sofia.log \
                              /utils/Sofia.real
EOF
chmod +x /utils/sofia.wrap.sh

# 3. Make /usr/bin/Sofia point at the wrapper. Squashfs is read-only,
#    so we use a bind mount - the inode of /usr/bin/Sofia is now
#    /utils/sofia.wrap.sh.
mount --bind /utils/sofia.wrap.sh /usr/bin/Sofia

# 4. Kill Sofia and SofiaRun.sh, then restart SofiaRun.sh manually -
#    XmServices_Mgr will not do this for us. The < /dev/null is
#    important: a backgrounded shell pipeline that contains a
#    ptraced-stopped child receives SIGTTIN if any process in it
#    tries to read from the controlling tty. /dev/null skips that.
killall Sofia SofiaRun.sh
sleep 1
/usr/sbin/SofiaRun.sh </dev/null >/dev/null 2>&1 &

# 5. Wait long enough for sensor init + a few seconds of steady state
sleep 55

# 6. Reboot. The bind mount is ephemeral and goes away cleanly.
reboot
```

`tools/capture_sensor.sh sofia` automates this through `expect(1)`:

```bash
tools/capture_sensor.sh sofia --host 10.216.128.106 --secs 55
```

**What `--skip=usleep` is for here:** Sofia spends a lot of time
busy-polling `nanosleep(0)` (yield) while it waits for various subsystems
to come up. With `--skip=usleep`, those entries don't pollute the trace,
and we still keep the structurally meaningful sleeps that matter for
phase segmentation. (Majestic uses real sleeps so `--skip=usleep` would
be lossy there; the script keeps it on by default.)

### Why these recipes don't transfer wholesale to other firmwares

The two camera-side recipes encode two different operational models:

| Concern | OpenIPC / Majestic | XiongMai HiLinux / Sofia |
|---|---|---|
| Streamer restart | `init.d` script | Manual via the supervisor parent |
| Supervisor behaviour | None — `init.d` script is fire-and-forget | Spawns once, doesn't restart |
| Hardware watchdog | Not in play during dev | Fed by supervisor; ~30 s to reboot |
| Streamer log destination | stdout (mixes with trace if not redirected) | Same |
| ELF kernel acceptance | Plain musl-static OK | Requires UPX |

For a third firmware, work out:

1. Does the kernel accept your ARM static ELF? If `sh: line 1: syntax error`
   on launch, UPX-pack.
2. Does anything respawn the streamer? Try `kill <streamer-pid>` and
   watch `ps`. If yes, you can let it respawn through your wrapper. If
   no, you'll need to start it manually after killing.
3. Is there a watchdog? `ls /dev/watchdog*`. If yes, time-bound your
   capture and have a clean exit path.
4. Does the streamer write to stdout? If yes, **always** use
   `--output=PATH` to avoid interleaving.

## Stage 3 — Post-processing

Three Python scripts, no external dependencies, all read/write under
`tools/dumps/` by convention.

### `trace_segment.py`

Splits the raw log into phases:

| Phase | What it contains |
|---|---|
| `pre_sensor` | Bus probe, MIPI/VI struct dumps, pre-init noise |
| `init` | From `sensor_write_register(0x100, 0x0)` (reset) to `sensor_write_register(0x100, 0x1)` (stream-on) |
| `post_init` | A short burst of AE/exposure prime writes between stream-on and the steady-state loop |
| `runtime` | Per-frame writes during steady-state (e.g. AE updating exposure registers) |

The init/post-init split exists for diff-friendliness: the AE loop in
`post_init` typically rewrites the same exposure registers (`0x3E00..`,
`0x320E/F`, …) that `init` had set to default values. Comparing
`init`-only against the reference's init function avoids spurious
mismatches.

```bash
python3 tools/trace_segment.py tools/dumps/cap.log
# wrote tools/dumps/cap.log.segments.json
#   pre_sensor   39 events
#   init         173 events
#   post_init    15 events
#   runtime      2585 events
```

### `trace_to_driver.py`

Emits HiSilicon-SDK-shaped C from the segmented JSON. Two functions per
sensor: `<sensor>_linear_init` and `<sensor>_post_init_exposure_prime`,
plus a comment block summarising the most-frequently-written runtime
registers (the AE/AGC hot list).

```bash
python3 tools/trace_to_driver.py tools/dumps/cap.log.segments.json \
                                 --sensor sc2315e \
                                 --out tools/dumps/cap.c
```

The output is a scaffold, not a finished driver:

- The `init` function is a register table you can hand-edit into the
  vendor's `sensor_init` callback.
- The `post_init_exposure_prime` function hints at what the SDK's
  `cmos_inittime_update` sets up.
- The runtime hot-register comment block tells you which registers
  belong inside the AE/AGC callback. The values driving them are
  **not** in the trace — only the regs touched and how often.

### `trace_diff.py`

Diff-by-register-write. Extracts every `sensor_write_register(reg, val)`
call (or the vendor-shaped `<sensor>_write_register(ViPipe, reg, val)`
variant) from each file and reports:

- address coverage (how many ref regs the trace actually wrote)
- value match (last value seen per address agrees)
- sequence similarity (LCS, order-aware)
- per-side asymmetric diffs and value mismatches

`--gen-scope` and `--ref-scope` restrict each side to a named function
body. Critical for the AE-overwrite issue mentioned above:

```bash
python3 tools/trace_diff.py \
    tools/dumps/cap.c \
    /path/to/smart_sc2315e/sc2315e_sensor_ctl.c \
    --gen-scope sc2315e_linear_init \
    --ref-scope sc2315e_linear_1080P30_init
```

## Reading the diff

For SC2315E captured from Majestic without scoping, the unscoped diff
shows ~5 value mismatches at registers `0x320E`, `0x320F`, `0x3E01`,
`0x3E02`, `0x3E09` — these are VMAX and exposure registers. They appear
in both `init` (defaults) and `post_init` (AE prime). The generator's
"last value seen" is the AE prime value, while the reference's init
function carries the default. This is **expected**, not a bug; scope
the diff to the `linear_init` function on both sides and the mismatches
disappear.

Other expected sources of mismatch on a real capture:

- **Registers in the reference source but not in the trace**: these are
  often gated by mode (HDR-only registers, sub-pipe configurations, OTP
  reads). The reference is the union over all branches; your trace
  exercises one. Cross-check with a second reference if you have one
  ([`sc_sc2315e`](https://github.com/widgetii/sc_sc2315e) does this for
  SC2315E).
- **Registers in the trace but not in the reference source**: these are
  binary updates the source missed (vendor patched the closed driver
  but not the published one) or runtime tuning that the trace happened
  to capture in `init` slot. Inspect manually — these are the
  interesting ones.
- **Order differences in LCS but ≥99% address match**: usually
  reorderings inside an unordered group of writes (BLC defaults,
  test-pattern setup). Not material for sensor function.

A 100 % triple match like the one in the worked example is the
**ceiling**, not the expected outcome. 90+/85+/80+ is a healthy result
that lets you trust the scaffold as a porting starting point.

## Troubleshooting

### `sh: line 1: syntax error: unexpected word (expecting ")")` on `ipctool` launch

The kernel returned `ENOEXEC` for your binary. Cause is almost always
"binary built with toolchain whose ELF flags this kernel doesn't accept".
Solution: UPX-pack the binary (see Stage 1). The CI toolchain alone is
not sufficient; UPX is the actual fix.

### Trace contains lines like `sensor_write_register(0x5785xmcap_video_api.c`

Streamer log lines and trace pseudocode were both written to the same
fd. Use `--output=PATH`. If you can't (older `ipctool` build), strip
ANSI escapes before parsing:

```bash
sed 's/\x1b\[[0-9;]*m//g' raw.log > clean.log
```

…and accept that some lines mid-write may be truncated.

### Trace shows lots of `error copy_from_process from 0 (I/O error)`

The exit-side hook tried to read the result buffer for a syscall but
the address was zero or unmapped. This usually means the syscall
returned an error before populating the buffer (e.g. an I2C read at an
unresponsive address during bus probe). Filter these out — they convey
no information.

### Trace is dominated by `usleep(0)`

Streamer is busy-polling. Pass `--skip=usleep` to silence them. You
will lose phase boundaries that depend on actual sleep durations, but
the segmenter falls back on the `0x100=0` / `0x100=1` reset/stream-on
markers, which is enough for most sensors.

### `expect`'d capture session ends with `Connection closed by foreign host` mid-flow

Either the camera rebooted under you (watchdog or `RebootAbnormal`
trigger after a stream death), or your kill sequence reaped a process
the supervisor depended on. For XiongMai cameras, do not kill
`XmServices_Mgr`; it provides services the streamer needs and feeds the
hardware watchdog. Use the bind-mount approach in the Sofia recipe.

### Diff shows 100 % address match but only 60 % sequence match

You're capturing a different SDK build than the reference was generated
from. Some vendors reorder writes between releases without changing
behaviour. The address+value match is what matters; sequence is a
helpful proxy that breaks down across versions.

## File layout

```
ipctool/
├── src/
│   ├── ptrace.c              # ARM-only ptrace engine, --output flag added
│   └── hal/hisi/ptrace.c     # HiSilicon-specific MIPI/VI struct decoders
├── tools/
│   ├── capture_sensor.sh     # build / majestic / sofia subcommands
│   ├── trace_segment.py      # parse + phase split
│   ├── trace_to_driver.py    # JSON → C scaffold
│   └── trace_diff.py         # generated vs reference, scoped
└── docs/
    └── sensor-driver-extraction.md  # this file
```
