# GPIO on a camera with ipctool

Four subcommands, and they differ in one way that matters more than any other:
**`gpio scan` and `gpio get` only read. `gpio set` and `gpio mux <pad> <func>`
write.** If you are exploring an unfamiliar board, stay on the first two.

```
ipctool gpio scan                  # read-only; watch every GPIO pad for changes
ipctool gpio get  <pad>            # read-only; level of one pad
ipctool gpio mux  <pad>            # read-only; what function the pad carries
ipctool gpio mux  <pad> <func>     # WRITES: re-mux the pad
ipctool gpio set  <pad> <value>    # WRITES: drive the pad
```

`<pad>` is accepted in either spelling, `5_6` (group 5, bit 6) or `46`
(linear, = group * 8 + bit). Both appear in `gpio scan` output.

## `gpio scan`

It prints a baseline table of every GPIO pad, then polls in a loop and reports
each bit that changes until you press Ctrl-C.

```
Gr: 0, Addr:0x120B00E4, Data:0x21 = 0bxx100xx1, Addr:0x120B0400, Dir:0x00 = 0bxx000xx0
Gr: 1, Addr:0x120B103C, Data:0x00 = 0bxxxx0000, Addr:0x120B1400, Dir:0x0C = 0bxxxx1100
...
======================================================================================
Waiting for while something changes...
======================================================================================
Gr:1, Addr:0x120B103C, Data:0x00 = 0bxxxx0000 --> 0x04 = 0bxxxx0100
Mask: "devmem 0x120B1010 32 0x04", GPIO1_2, GPIO10, Dir:Output, Level:1
```

An `x` in the binary column is a pad that is **not** muxed to GPIO, so the scan
is not watching it. A `0` or `1` is a pad it is watching, and its current level.

Read that last line as: pad `GPIO1_2`, which is `GPIO10` in linear numbering,
is an **output** and just went high; `devmem 0x120B1010 32 0x04` is the command
that reproduces the write by hand.

### It does not drive anything

`gpio scan` issues no writes at all. It reads two registers per group — the
data register and, at `+0x400`, the direction register — and never touches
either. It does not tri-state pins, does not change any pad's direction, and
does not change any pad's mux. **Nothing it does can damage an IO.**

Note that many vendor firmwares ship a separate `gpio` applet of their own,
unrelated to ipctool, and some of those *do* drive pins. This page is only
about `ipctool gpio`.

### Why your toggling may show nothing

Three reasons, in the order worth checking:

1. **The pad is an output.** Reading the data register of an output returns the
   value the SoC is *driving*, not the voltage on the pin. Stimulating it
   externally changes nothing you can read — and fighting a driven output with
   an external source is how pads get damaged, so check the baseline table
   first. The `Dir:` column tells you which pads are inputs.

2. **The pad is not muxed to GPIO.** `gpio scan` only watches pads whose mux is
   currently set to their GPIO function — it builds its mask from the pad-mux
   tables (`docs/padmux.md`), and a pad carrying I2C, PWM or a sensor signal is
   not in it. `ipctool gpio mux <pad>` tells you what a given pad carries, and
   `ipctool reginfo` dumps the lot. A pad that shows nothing in the scan
   because it is muxed elsewhere is the normal case on a populated board, not a
   fault.

3. **The pulse was too short.** The loop polls every 100 ms, so a stimulus
   shorter than that can fall between two samples. Hold the state for a second.

### Recording a session

`ipctool gpio scan | tee gpio.log` works. It did not before — the command never
exits normally, so a block-buffered stdout was never flushed and a redirected
capture came out empty.

### Finding the IR-cut pins

Start with what ipctool already guesses. The plain report carries a
`possible-IR-cut-GPIO` line in the `board:` section:

```yaml
board:
  vendor: OpenIPC
  possible-IR-cut-GPIO: 10,11
```

That is a heuristic — outputs in a group the streamer has mapped, or otherwise
outputs sitting low — and IR-cut is nearly always a *pair* of pads, driven in
opposite directions to flip the filter. To confirm a candidate pair, run
`gpio scan` and switch the camera between day and night mode in the web UI:
whatever the firmware drives will appear as two lines that move together.

That is the reliable direction of travel. Driving a suspected pad with
`gpio set` to see what happens works too, but you are then writing to a pad
whose function you have not confirmed — see the warning below.

### Numbering in other tools

The linear number (`GPIO10`) is what the OpenIPC web UI, Majestic's config and
the kernel's `/sys/class/gpio` all use. The `group_bit` form (`GPIO1_2`) is
what the SoC data sheets use. `gpio scan` prints both on every line so you do
not have to convert.

## `gpio mux`

With no function name it reports, with one it writes:

```
# ipctool gpio mux 44
GPIO5_4 (44): LED0_MODE_1
# ipctool gpio mux 44 GPIO          # WRITES
```

**Check before you claim a pad.** On SigmaStar the mux is peripheral-centric:
the register field says which pad a *function* comes out on, so muxing function
F onto pad X silently takes F away from pad Y if it was already routed there.
Read the function's current field first. On HiSilicon and Goke the mapping is
one register per pad and this particular trap does not apply, but the general
rule does: a pad on a working board is carrying something, and taking it away
stops whatever that was. `docs/padmux.md` has the mechanisms per vendor.

If you do re-mux something by accident, `gpio mux <pad> <original function>`
puts it back; on SigmaStar restoring a pad to "idle but not GPIO-asserted" also
needs one `devmem` to clear the pad's GPIO-enable bit, which `gpio mux` has no
way to express.
