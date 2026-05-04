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
| `init` | From the sensor's standby/reset write to its stream-on write (see "Sensor-family init patterns" below) |
| `mode_switch_N` | Each subsequent stream-off → reconfigure → stream-on cycle |
| `post_init` | A short burst of AE/exposure prime writes between stream-on and the steady-state loop |
| `runtime` | Per-frame writes during steady-state (e.g. AE updating exposure registers) |

The init/post-init split exists for diff-friendliness: the AE loop in
`post_init` typically rewrites the same exposure registers (`0x3E00..`,
`0x320E/F`, …) that `init` had set to default values. Comparing
`init`-only against the reference's init function avoids spurious
mismatches.

#### Sensor-family init patterns

Different sensor vendors gate "stream on" through different registers.
The segmenter has a small table of `(family, reg, init_val, stream_val)`
patterns and tries each in order; the first that finds both endpoints
in a trace wins. The matched family is recorded as `init_pattern` in
the segments JSON.

| Family | Register | Init value | Stream-on value | Sensors |
|---|---|---|---|---|
| `smartsens` | `0x0100` | `0x00` (reset) | `0x01` (stream-on) | SC2315E, SC2335, SC*, SmartSens generally |
| `sony_imx`  | `0x3000` | `0x01` (standby) | `0x00` (release) | IMX291, IMX385, IMX307, Sony IMX line |
| `soi_jx`    | `0x0012` | `0x40` (standby/reset) | `0x00` (stream-on) | JXF22, JXF23, JXH62, SOI / JX 8-bit register family |

Adding a family is one entry in `INIT_PATTERNS` at the top of
`trace_segment.py`. If your trace is recognised but no init phase is
detected, your sensor probably uses a third pattern — write the
addresses and values in here and the segmenter will pick it up.

If no pattern matches, the segmenter emits everything as `pre_sensor`
and the generator skips the function-body emission. Most often that
means your sensor uses a third stream-control register convention not
yet in `INIT_PATTERNS`. Check the raw trace for the obvious bracket
(a register written once near the start, then again near the end with
the opposite value) and add an entry.

### Decoder coverage across HiSilicon families

Different HiSilicon families take different paths to the I2C bus.
ipctool's ptrace decoder handles each:

| Family | Sensor driver path | What ipctool decodes |
|---|---|---|
| HISI_V1 | `ioctl(/dev/hi_i2c, CMD_I2C_WRITE, &I2C_DATA_S)` | `xm_i2c_*` callbacks decode the structured payload |
| HISI_V2 / V2A | `write(/dev/i2c-X, buf, reg+data)` little-endian, after `I2C_16BIT_REG/DATA` ioctls | `i2c_write_exit_cb` infers widths from `nbyte`, picks LE for V2/V2A; `hisi_gen2_ioctl_exit_cb` decodes `I2C_SLAVE_FORCE` |
| HISI_V3 / V3A / V4 / V4A | `ioctl(/dev/i2c-X, I2C_RDWR, &i2c_msg)` big-endian | `hisi_i2c_read_*_cb` decodes the rdwr message |

uClibc on some V1/V2 firmwares wraps the libc `write()` call as a
single-iovec `writev()` rather than direct `__NR_write`. ipctool
handles both — `syscall_writev_exit` decodes the iovec and forwards
to the same fd callback as plain `write()`.

When threads share an fd table (`CLONE_FILES`, the standard for
multi-thread streamers), opening a fd in one thread makes it usable
in all of them. ipctool maintains this invariant explicitly: on
`open()` it broadcasts the new fd state to every tracked process; on
`close()` it clears it everywhere. Without this, a thread peer's
write on a fd opened by the parent silently drops to no callback.

### When the trace is empty anyway

A V1/V2 capture that shows `0` `sensor_write_register` lines despite
the streamer reporting init success usually means one of:

* **`ipctool` segfaulted mid-trace.** Symptom signature: trace ends
  at exactly `i2c_read()` (or shortly after the `i2c-N` banner), no
  further events, `wait $!` reports exit status 139 (SIGSEGV). The
  streamer keeps running untraced, so its real register-write burst
  goes unobserved and the captured log stays at ~10 lines. To confirm,
  capture a core dump and inspect the backtrace:

  ```bash
  ssh root@<camera> "ulimit -c unlimited; cd /tmp; \
    /tmp/ipctool trace --output=/tmp/dumps/trace.log /usr/bin/majestic"
  scp root@<camera>:/tmp/core /tmp/core
  arm-linux-gdb --batch -ex 'core /tmp/core' -ex 'bt full' \
    build-arm-ci/ipctool
  ```

  If the backtrace lands inside a decoder callback, the bug is in
  `src/ptrace.c`'s callback (most likely a NULL-deref on the
  `copy_from_process` buffer or filename argument). The historical
  example was `hisi_gen2_read_exit_cb` doing `memset(&buf, 0, …)`
  instead of `memset(buf, 0, …)`, nullifying the pointer before
  `copy_from_process` was called - this killed Hi3518EV200 +
  libsns_jxf22.so traces immediately after the first sensor-ID read.
* **Sensor `.so` opens its own I2C handle in a path our trace
  doesn't see.** Check `/proc/<streamer-pid>/fd` while it's running:
  if the live `/dev/i2c-N` fd in the running process is different
  from the one the trace caught (or arrived later than the kill),
  capture for longer.
* **Sensor `.so` uses a HiSilicon-specific `/dev/*` device that
  isn't in our dispatch table** (e.g. `/dev/sys`, `/dev/sns_drv0`).
  The signature is the trace ending shortly after `i2c-N` banner
  with no writes; live `/proc/<pid>/fd` shows the unfamiliar device
  open. Add it to `syscall_open`'s dispatch in `src/ptrace.c`.
* **Sensor `.so` invokes a ptrace-incompatible code path** (some
  vendor binaries detect ptrace and skip the writes; rare).

```bash
python3 tools/trace_segment.py tools/dumps/cap.log
# wrote tools/dumps/cap.log.segments.json
#   pre_sensor   39 events
#   init         173 events
#   post_init    15 events
#   runtime      2585 events
```

### `trace_to_driver.py`

Emits HiSilicon-SDK-shaped C from the segmented JSON. Three functions
per sensor:

- `<sensor>_linear_init` — the cold-init register table.
- `<sensor>_post_init_exposure_prime` — the AE/exposure values applied
  once when the AE loop first kicks in.
- `<sensor>_ae_step` — a per-frame AE callback skeleton listing the
  registers the AE loop hits in steady state, with their last-seen
  values as placeholders. The math driving those values (gain LUTs,
  exposure scaling, threshold branches) is not in the trace; the
  skeleton points the reverse-engineer at the vendor's
  `cmos_inttime_update` / `cmos_gains_update` equivalents to fill in.

For SC2315E captured under steady ambient light, the `_ae_step` body
matches the **else-branches** of the reference's `cmos_inttime_update`
(`0x3314=0x14`) and `cmos_gains_update` (`0x5781=0x60`, `0x5785=0x30`)
— exactly the registers and values the running AE loop wrote each
frame. A capture under varying lighting would surface the if-branches
too; the skeleton would then need a conditional that you'd derive by
hand from the value distribution.

For value-distribution data without a fresh trace, use `ipctool sensor
monitor` (see "Live-reading the AE state" below).

The output is **standalone-buildable** — it includes a small "SDK stubs"
block (typedef for `VI_PIPE`, a no-op `sensor_write_register`) so that
`gcc -fsyntax-only` and `gcc -c` succeed without the vendor headers.
Delete that block and replace it with `#include "hi_comm_video.h"` /
`#include "hi_sns_ctrl.h"` plus the vendor's bus-aware
`sensor_write_register` to integrate into a HiSilicon SDK build.

`tools/test_pipeline.sh` runs the full segment → generate → compile flow
end-to-end on a synthetic trace and is wired into CI
(`pr-build-check.yml::test-extraction-pipeline`), so a regression in
any of the three Python scripts that breaks the generator output is
caught at PR time.

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
- The `_ae_step` function is the runtime AE/AGC callback skeleton:
  the registers the AE loop touches every frame, with their last-seen
  trace values as `/* TODO: derive */` placeholders. The math driving
  those values is **not** in the trace — see "Live-reading the AE
  state" below for how to capture value distributions.
- File-scope MIPI/VI struct declarations (e.g.,
  `combo_dev_attr_t SENSOR_ATTR = { … }`,
  `pstViDevAttr VI_DEV_ATTR_S = { … }`) are emitted between the SDK
  stubs and the init function, wrapped in `#if 0 / #endif`. Standalone
  builds skip them (the references to vendor enums like
  `INPUT_MODE_MIPI` aren't visible without `hi_mipi.h`); when
  integrating into a HiSilicon SDK build, remove the `#if 0/#endif`
  to drop the struct definitions in alongside the rest of the
  scaffold.

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

## Capturing mode switches

A "mode switch" here is a runtime sensor reconfiguration — switching
1080p25 to 720p, or linear to WDR — without a full streamer restart.

The capture-side mechanism is streamer-specific. **OpenIPC Majestic
does not support runtime mode switching**: configuration changes go
through `/etc/sensors/*.ini` files and require a streamer restart, so
each mode is a separate cold-init capture. **XiongMai Sofia does**
support several runtime knobs via the DVR-IP TCP protocol on port
34567; the [python-dvr](https://github.com/OpenIPC/python-dvr) client
exposes them. Example, toggling Sofia's `BroadTrends.AutoGain` knob:

```python
from dvrip import DVRIPCam
cam = DVRIPCam('10.216.128.106', user='admin', password='')
cam.login()
cam.set_info("Camera.ParamEx.[0]",
             {"BroadTrends": {"AutoGain": 1, "Gain": 50}})
```

Whether a given knob actually causes a sensor-side reconfigure is
**sensor-specific** — Sofia's BroadTrends path lands in software-side
gain control on most sensors and only triggers a sensor-side WDR-mode
change on sensors whose firmware has a separate WDR variant. As a
data point, when toggling `AutoGain` 0→1→0 on the SC2315E camera at
`10.216.128.106` while `ipctool trace` was watching, the trace shows
**zero** additional `0x100` cycles after init — the sensor stays in
linear mode regardless. Sofia's supported-sensor list confirms this:
`SC2315_WDR` is a separate entry from `SC2315E`, so no WDR firmware
exists for our test SoC. To exercise mode-switch capture end-to-end,
use a sensor that Sofia knows in `_WDR` form (SC2315, IMX307, etc.).

### Segmenter heuristic

`trace_segment.py` detects mode switches by watching for a `0x100=0`
write **after** init has completed (`init_end`), paired with the next
`0x100=1` to form a `mode_switch_N` phase. Multiple cycles produce
`mode_switch_1`, `mode_switch_2`, etc. The post-init AE prime and
runtime steady state then anchor on the *last* `0x100=1`, so a trace
with no mode switch is identical to before.

`trace_to_driver.py` emits one `<sensor>_set_mode_N` function per
mode-switch phase, in the same shape as `_linear_init`.

If a sensor hot-swaps modes via a group-hold (e.g. `0x3812=0x00 ...
0x3812=0x30` block) without toggling `0x100`, this heuristic misses
the boundary — extend the segmenter when you hit such a sensor.

## Stage 4 — Live-reading the AE state with `ipctool sensor monitor`

`ipctool sensor` is a built-in subcommand (separate from `trace`) that
reads a fixed list of AE/exposure registers from the running sensor
over I2C/SPI in a loop, decoded as labelled fields. Same idea as
`_ae_step` but read-side: instead of inferring AE registers from a
captured trace, you can poll the actual sensor while it's running.

```console
$ ipctool sensor monitor
EXP   100  AGAIN  310  DGAIN  80  VMAX  546  R3301  f  R3314  14  R3632  8  HOLD  0  R5781  60  R5785  30
EXP   100  AGAIN  310  DGAIN  80  VMAX  546  R3301  f  R3314  14  R3632  8  HOLD  0  R5781  60  R5785  30
EXP   ff   AGAIN  330  DGAIN  80  VMAX  546  R3301  f  R3314  14  R3632  8  HOLD  0  R5781  60  R5785  30
                                                                                ^^^^^^^^^^^^^^^^^^^^^^^^^
                                                            same hot regs trace_to_driver picked up
```

The reg list per supported sensor lives in `src/snstool.c` (a small
table, ~10 entries). For SC2315E that table mirrors what `_ae_step`
emits — both are the registers the running AE loop writes per frame.

### Pairing `sensor monitor` with `_ae_step`

The trace-and-extract workflow gives you the **register set** of the
AE loop. `sensor monitor` gives you **value time-series** under
whatever lighting conditions you can produce. Pair them:

1. Extract `_ae_step` via `trace`. Note the placeholder values
   (e.g., `0x5781=0x60`, `0x3314=0x14` on SC2315E).
2. Cover/uncover the lens, point at varying scenes, switch the IR cut
   etc. while running `ipctool sensor monitor` and recording the
   output (`tee monitor.log`).
3. Plot or grep the resulting log for value transitions on the
   `_ae_step` registers.
4. Derive the conditional / LUT that maps trace inputs (often gain
   index, integration time) to those values. This is the manual step
   that no register-trace tool can automate.

For SC2315E, the reference's `cmos_gains_update` writes
`0x5781=1, 0x5785=2` once gain ≥ a fixed threshold; the rest of the
time it writes the `0x60, 0x30` we captured. A varying-light
`monitor` session that pushes the AE into high-gain regime will show
that transition directly.

### Adding a new sensor to `monitor`

Edit `src/snstool.c`:

1. Add a `Reg[]` table for the sensor: name, address, byte length.
   Cover the AE-modified registers (exposure, gain, vmax) plus any
   tuning registers that change per-frame in `cmos_gains_update`.
2. Add an entry to `sns_regs[]`: sensor ID (uppercase, matching
   `ctx->sensor_id` from `src/sensors.c`), pointer to the reg
   table, and `.be = 1` if the sensor is big-endian on multi-byte
   registers.
3. The dispatch in `monitor()` is automatic — no other glue needed.

When `_ae_step` emits a register set you didn't have in `monitor`,
that's a good cue to extend the `monitor` table to match. A nice
property of the workflow: the trace tells you which registers the
running streamer cares about, the monitor lets you watch them change
in real time.

## Reading the diff

For SC2315E captured from Majestic without scoping, the unscoped diff
shows ~5 value mismatches at registers `0x320E`, `0x320F`, `0x3E01`,
`0x3E02`, `0x3E09` — these are VMAX and exposure registers. They appear
in both `init` (defaults) and `post_init` (AE prime). The generator's
"last value seen" is the AE prime value, while the reference's init
function carries the default. This is **expected**, not a bug; scope
the diff to the `linear_init` function on both sides and the mismatches
disappear.

### Triangulating against multiple references

When a sensor has more than one published source — e.g. an OpenIPC port
**and** an older reverse-engineering effort — diffing against both lets
you triangulate registers that appear in only one as either:

- **In trace + only in ref-A**: ref-B is incomplete (the RE missed
  this register, or the port pruned it).
- **In trace + only in ref-B**: ref-A drifted from the binary
  (vendor patched the closed driver, port didn't follow).
- **In both refs but not in trace**: probably dead code in both refs,
  or behind a build flag the trace didn't exercise.
- **In trace and both refs**: high confidence, ship it.

For SC2315E specifically, the four artifacts available — the trace
from Majestic, the trace from Sofia, `widgetii/smart_sc2315e`
(OpenIPC port from SC2231 SDK template), and `widgetii/sc_sc2315e`
(older RE port from SC2235 SDK template) — agree byte-for-byte on
init: 172 writes, 169 unique addresses, identical values, identical
order, every pair-wise comparison at 100/100/100%.

```bash
# OpenIPC port
python3 tools/trace_diff.py generated.c \
    /tmp/smart_sc2315e/sc2315e_sensor_ctl.c \
    --gen-scope sc2315e_linear_init \
    --ref-scope sc2315e_linear_1080P30_init

# Reverse-engineered port (note: int return, sensor_write_register_0)
python3 tools/trace_diff.py generated.c \
    /tmp/sc_sc2315e/sc2235_sensor_ctl.c \
    --gen-scope sc2315e_linear_init \
    --ref-scope sc2235_init
```

`trace_diff.py` accepts both `void <name>(...)` and `int <name>(...)`
function definitions, and its register-write regex matches both
`sensor_write_register(...)` and `sensor_write_register_0(...)`
(the bus-numbered suffix used by the older RE port).

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

### Narrowing "fd N is open live but not in trace" with `IPCTOOL_TRACE_DEBUG=1`

Setting this in the tracee's environment makes `ipctool trace` log
every `open()` and `write()` syscall to stderr (filename, fd, callback
state). Off by default, zero overhead unless set. Useful when
`/proc/<streamer-pid>/fd` shows `/dev/i2c-N` open but the trace
contains no banner/writes for it - the dbg log will say whether
`syscall_open` ever ran for that fd and what filename it resolved.

```bash
IPCTOOL_TRACE_DEBUG=1 ipctool trace --output=/tmp/trace.log \
    /usr/bin/majestic 2>/tmp/trace.dbg
grep -E "i2c|fd=26" /tmp/trace.dbg
```

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
