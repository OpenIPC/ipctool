# Pad multiplexing

Every SoC pad can be several things, and which one it is right now is a
hardware register. `libipchw` publishes that through five calls in
`include/ipchw.h` -- three lookups over constant tables, and two accessors
that touch `/dev/mem`:

```c
int ipchw_padmux_by_func(const char *func_name, ipchw_padmux_t *out, int max);
int ipchw_padmux_by_prefix(const char *prefix, ipchw_padmux_t *out, int max);
int ipchw_padmux_by_pad(int pad, ipchw_padmux_t *out, int max);
int ipchw_padmux_get(int pad, ipchw_padmux_t *out);
int ipchw_padmux_set(int pad, const char *func_name);
```

`ipctool reginfo --pads` is all five in one command, and is what to run first
on a board of a family whose table has just been entered. `ipctool gpio mux
<pad>` asks about one pad and `ipctool gpio mux <pad> <function>` changes it;
`<function>` may be a name or a selector value, and `GPIO` is the name that
puts the pad back.

> Until 2026-09 the bare `gpio mux <pad>` form did **not** ask -- it muxed the
> pad to GPIO. A pad that a MISC-style table drop hides from `reginfo --pads`
> still answers here, which is the one place to see it.

## Three vendors, three mechanisms

This is the thing to understand before touching any of it. The API looks like
one question, and underneath it is three different pieces of silicon.

### HiSilicon and Goke -- a selector per pad

One register per pad; a field in its low nibble picks which function the pad
carries. `src/reginfo.c` holds 1333 rows of it as `muxctrl_reg_t`, where
`funcs[]` is **indexed by the selector value** and `"reserved"` fills the
holes so the later indices stay right:

```c
MUXCTRL(EV200_iocfg_reg4, 0x100C0010, "GPIO0_4", "PWM1", "UART1_RXD",
        "I2C1_SDA")
```

Everything the API promises is true here and only here: one register write
selects a function (`IPCHW_PADMUX_F_RMW`), and every alternative of the pad is
in that same field, so one read says which is live
(`IPCHW_PADMUX_F_SHARED_REG`).

**The holes are load-bearing, and a data sheet states them twice.** These rows
are hand-entered, and the document says the same thing in two chapters that do
not agree:

| chapter | shape | for `muxctrl_reg4` |
|---|---|---|
| 2.3 Pin Multiplexing Control Registers | the bit field, value by value | `000: GPIO2_0`, `001: RMII_CLK`, `011: VO_CLK`, `100: SDIO1_CCLK_OUT` |
| 2.4 Software Multiplexed Pins | columns headed "Multiplexed Signal 0", "Signal 1", "Signals 2-4" | `GPIO2_0`, `RMII_CLK`, `VO_CLK`, `SDIO1_CCLK_OUT` |

Chapter 2.3 is the encoding: `010` is not assigned, so `VO_CLK` is 3 and
`SDIO1_CCLK_OUT` is 4. Chapter 2.4 lists the alternatives by **position**,
with the holes closed up, and its last column is a *range* (`2-4`) that never
says which is which. Transcribe that one and every function after a hole is
off by one -- which is what happened to Hi3518EV20X and Hi3516CV200, on 19
registers each, and cost the reporter of issue #135 a working WiFi module
until they read the registers off their own stock firmware.

`tools/check_hisi_padmux.py` diffs a table against chapter 2.3 and will
`--fix` a missing hole (only a hole -- a renamed function makes it refuse, on
the grounds that the parse is then more likely wrong than the table). Data
sheets are not in this repo, so CI cannot run that; `--selftest` runs the
parsing and the row verdicts against a built-in fixture instead, and
`tools/test_pipeline.sh` runs it. The fixture is written in the shape
`pdftotext -layout` actually emits, because every case in it is one that has
silently produced a wrong answer: a value list split by a page break, a
one-bit field, a slashed name, a spelled-out `11: reserved`, and a bit
diagram whose clipped label reads `muxctrl_reg2` when it means `muxctrl_reg22`.

Where it has been run:

**The chapter moves house at Hi3516CV300.** Up to Hi3518EV20X it is chapter
2.3 of the data sheet PDF; from Hi3516CV300 onwards the PDF only mentions
`iocfg_reg55` in passing and the definitions live in the chip's
`<chip>_PINOUT_*.xlsx` instead -- sheet `3.muxctrl_reg Description` on
Hi3516CV300, `3. Pin Control Registers` after it, `3.管脚控制寄存器` in the
Chinese editions, which for Hi3516DV200/EV200/EV300 and Hi3518EV300 are the
only editions shipped -- and `3.管脚复用寄存器` in the older Chinese
Hi3516CV300, Hi3516EV100 and Hi3536D workbooks, and `3.Pin MUX Registers` on
Hi3536C. Two sheets sit next to it and are not the mux: the drive-strength
list (`4. Pin Drive Registers`, `4. pad_ctrl_reg Description`,
`4.管脚驱动能力寄存器`) and, on the Hi3536 parts, `5. Hardware MUX
Relationship`. `pick_sheet()` refuses rather than choose when more than one
name could be it, and `--sheet` settles it. Pass a workbook with `--pinout` and a PDF with
`--datasheet`. The workbook is the better source: it states absolute
addresses, so there is no `--base` to get wrong and a table spread over
several banks needs no special handling.

| table | SoC | rows | result | source |
|---|---|---|---|---|
| `CV100` | Hi3518E V100 | 87 | agrees | PDF, SPC081 + SPC0B0 |
| `AV100` | Hi3516A/D V100 | 123 | 2 holes restored | PDF, SPC050 + SPC080 |
| `EV20X` | Hi3518E V20X | 66 | 19 holes restored | PDF, SPC040 + SPC050 |
| `CV200` | Hi3516C V200 | 66 | 19 holes restored | PDF, SPC040 + SPC050 |
| `CV300` | Hi3516C V300 | 66 | agrees | workbook, EN + CN + SPC010 |
| `EV200` | Hi3516E V200 | 52 | agrees | workbook, SPC011 + SPC012 |
| `EV300` | Hi3516E V300 | 93 | agrees | workbook, SPC011 + SPC012 |
| `_8EV300` | Hi3518E V300 | 56 | agrees | workbook, SPC011 + SPC012 |
| `DV200` | Hi3516D V200 | 99 | agrees | workbook, SPC011 + SPC012 |
| `CV500` | Hi3516C V500 | 104 | agrees | workbook, EN + CN |
| `DV300` | Hi3516D V300 | 113 | agrees | workbook, EN + CN |
| `RCV100` | Hi3536C V100 | 77 | agrees | workbook, EN + CN |
| `DV100` | Hi3536D V100 | 47 | agrees | workbook, EN + CN |
| `AV200` | Hi3519 V101, Hi3559/Hi3556 V100, Hi3516A V200 | 106 | **91 of 106**; the checker exits non-zero | workbook, EN + CN |

Every row above **except `AV200`** was run against two independent editions --
two data sheet revisions, or an English and a Chinese workbook, or two SDK
releases -- and agrees on every selector value, with the checker exiting 0.
`AV200` does not, and the row says so: the only workbook covers two of its
four SoCs and 15 rows are unsettled, so `check_hisi_padmux.py` reports them
and exits 1. Read that row as "no defect found", not as "verified". Among
the ones that do agree, the only difference anywhere is a name
(`muxctrl_reg93` on AV100 is `RMII_CLK` in SPC050 and
`RMII_CLK_OUT/MII_TX_CLK` in SPC080, and the table carries the newer
spelling). `CV300`'s count is 66 against 65 registers because
`CV300_muxctrl_reg66` is the `IPCHW_PADMUX_ADDR_NONE` sentinel, which no
document has and is not supposed to.

Still open:

- **`CV610`** (76 rows) disagrees with its workbook and is **not** a missing
  hole: functions are absent, renamed, or merged into one slashed entry where
  the document gives two values. It also matters which package -- the BGA
  sheet disagrees on 65 rows, the QFN sheet and the Hi3516CV608 sheet on 24
  each -- so which part the table was entered from has to be settled before
  any of it is rewritten.
- **`DV500`** (Hi3519D V500 and Hi3516D V500) is worse: 131 registers in the
  workbook against 102 rows, and many of the rows it does have carry
  `"reserved"` where the document names a real function, GPIO names included.
  It needs re-entering from the document rather than patching.
**`AV200` is partly verified, and the 15 unsettled rows are not a formality.**
It serves four SoCs and the only workbook for any of them covers Hi3559 V100
and Hi3556 V100. All 106 registers are there at the same addresses and 91
rows match outright. The other 15 differ in one direction only: the table
names an **Ethernet** function -- `RGMII_*`, `RMII_CLK`, `MDIO`, `EPHY_CLK`,
`EPHY_RSTN` -- where that document has nothing or `reserved`.

The likely reading is that Hi3559/Hi3556 V100 are *HD Mobile Camera* parts
that do not bring Ethernet out, while Hi3519 V101 and Hi3516A V200 do and are
what the table was entered from -- a part-variant difference rather than an
error. It is a reading, not a verification: nothing here rules out a hole
having been dropped inside those 15 rows the same way 19 were dropped from
each V2 table, because the document that would show it describes a part that
has no such pins. Treat Ethernet selectors on this family as unconfirmed.

`Hi3519V101_PINOUT_CN.xls` settles it. Its own user guide names it and no
release here ships it; note the `.xls`, which `tools/xlsx_min.py` cannot read
without converting first.

SDK source is no substitute for either: it shows the values a driver
*writes*, and a hole is exactly the value nobody writes.

### SigmaStar -- a selector per peripheral

There is no per-pad selector at all. A field belongs to a *peripheral* and its
value picks which **group of pads** carries it. `reg_fuart_mode` is three bits
of one chip-top register on Infinity6B0:

| value | pads that become FUART |
|---|---|
| 1 | `PAD_FUART_RX`, `PAD_FUART_TX`, `PAD_FUART_CTS`, `PAD_FUART_RTS` |
| 2 | `PAD_GPIO0`, `PAD_GPIO1`, `PAD_GPIO2`, `PAD_GPIO3` |
| 4 | `PAD_SD1_IO0`..`PAD_SD1_IO3` |

So one pad's alternatives are fields in *different* registers, the groups
overlap, and a pad is plain GPIO when nothing claims it. Consequences:

- Rows carry `IPCHW_PADMUX_F_RMW` but not `F_SHARED_REG`, and `gpio_func` is
  -1 on every one of them: getting back to GPIO means dropping every claim,
  which is not one write. Use `ipchw_padmux_set(pad, IPCHW_PADMUX_GPIO)`.
- The field is **in place** and several share a register. I2C0 and I2C1 are
  `[2:0]` and `[5:4]` of `0x1f203c24`; a mask applied at the wrong offset
  takes away an I2C0 routed to a completely different pad.
- A claim is dropped only when it is actually asserted, because the field is
  shared with the rest of its group.
- A handful of modes are selected by a **zero** field (the boot SPI and IR
  pads), which is also what an idle field reads as. `get()` believes a
  positive claim first; `set()` drops those by writing the whole field.
- The registers are 16-bit RIU ports in four-byte slots whose upper half is
  **not mapped**, so every access uses `OP_*_16`. A 32-bit store there writes
  two bytes that do not exist.

- **`m_stPadMuxTbl` is not the whole truth.** Some pads are muxed from banks
  the table never names -- an Ethernet pair by `REG_ETH_GPIO_EN` in ALBANY2, a
  USB pair by the UTMI0 power-down bits -- and the vendor routes exactly those
  through `HalPadSetMode_MISC()`. Their rows are still in the table, and on
  infinity6e they are copy-paste from the pad above them: `PAD_ETH_RN`
  through `PAD_USB2_DP` each claim `SPIHOLDN_MODE` and `EMMC0_8B_MODE_1`
  through **`PAD_SPI_HLD`'s own fields**. Believing them reports six pads as
  carrying whatever the flash HOLD pin carries, and offers to put eMMC data
  lines on the Ethernet magnetics. The generator now reads the MISC dispatch
  and drops a pad's rows when they share no register with what MISC actually
  writes for it -- overlap, not containment, because the vendor is also
  inconsistent about which of a group's modes it lists. Such a pad is listed
  with no modes at all: `by_pad()` returns nothing, `get()` says "cannot say"
  and `set()` refuses.

`src/hal/sstar_reginfo.h` is *not* this. It is the per-pad OEN/OUT/IN
registers of the GPIO controller, which is what `reginfo` dumps on SigmaStar
and what it has always been. Bit 0 of those per-pad registers is the **live
input level**, so a pad's register legitimately changes value with no one
writing it -- do not treat a diff there as a failed restore.

### Ingenic -- four bits in four registers

Pad-centric again, but there is no field: `INT`, `MSK`, `PAT1` and `PAT0`
carry one bit each at the pin's own position, and the nibble they spell is
`enum gpio_function` -- 0..3 the four device functions, 4 and 5 GPIO driven
low and high, 6 GPIO input, 8..11 an interrupt source.

So no register write selects a function. Rows carry no `IPCHW_PADMUX_F_RMW`,
`address` is `IPCHW_PADMUX_ADDR_NONE` and `func_mask` is 0 -- which is
deliberately the safe encoding, because a consumer that composes
`(old & ~0) | func` from such a row writes the register back unchanged rather
than aiming somewhere wrong.

Writes go through the set and clear aliases at `+4` and `+8` of each register,
so one pin changes without reading the other thirty-one, and then the port's
group number goes to `PZGID2LD` at `+0xF0`, which commits the four bits
together. Skipping that commit walks the pad through every intermediate
nibble, half of which are other device functions.

Direction is part of the same nibble as the mux, so "make this GPIO" has to
pick one: a pad already held as GPIO keeps what it had, one coming off a
peripheral becomes an input.

## Pad numbers and names

`gpio_pad` is the number the kernel uses, and it is the number to speak to
every consumer:

| vendor | pad number | `gpio_name` |
|---|---|---|
| HiSilicon/Goke | `bank * 8 + pin`, parsed out of the `GPIO<b>_<p>` entry | `"GPIO5_2"` |
| SigmaStar | the vendor pad id, which is the gpiochip index (base 0) | `"PAD_SR_IO03"` |
| Ingenic | `port * 32 + pin` | `"PB25"` |

Function names are the vendor's own and are **not portable**, not even within
HiSilicon: `PWM_OUT0` on V1, `PWM0` from V2, `PWM0_OUT1` on V5. `"PWM"` also
prefixes `SVB_PWM` and `PMC_PWM`, which are different controllers, so match
anchored. Across vendors it is worse: SigmaStar names a bus and not a wire
(`I2C1_MODE_3` claims two pads and does not say which is SCL), and Ingenic
calls I2C `SMB` and SPI `SSI`. Where the function name does not say which line
a pad carries, `gpio_name` often does -- `PAD_I2C1_SCL`, `PAD_SPI0_DI`.

## Regenerating the tables

The HiSilicon rows are hand-entered from datasheets and stay that way. The
other two are generated, because 2900 rows is past the point where
transcription is honest -- and because the generator can *check* things a
reader cannot, such as the fact that every row of a SigmaStar mode carries the
same register, mask and value.

```sh
# SigmaStar: from the vendor kernel's drivers/sstar/gpio/<family>/
tools/gen_sstar_padmux.py \
    --kernel /path/to/sstar/kernel --kernel /path/to/other/kernel \
    --family infinity6b0 --family infinity6e --family infinity6c \
    > src/hal/sstar_padmux.h

# Ingenic: one --spec per --soc, and each SoC says which KIND of source it
# takes -- see the table below
tools/gen_ingenic_padmux.py \
    --soc T31 --spec T31_H.3_gpio_spec.pdf \
    --soc T21 --spec .../soc-t21/include/mach/platform.h \
    --soc T23 --spec .../soc-t23/include/mach/platform.h \
    --soc T40 --spec .../boot/dts/ingenic/t40-pinctrl.dtsi \
    > src/hal/ingenic_padmux.h
```

Each generated header carries the exact command that made it, so the recipe
above is only the shape; the header is the record.

Both generators take `--verify <header>`, which re-derives and diffs instead
of writing. That needs the SDK, so CI cannot run it;
`tools/gen_sstar_padmux.py --selftest` runs the vendor-source parsing against
a built-in fixture instead, needs nothing, and is what `tools/test_pipeline.sh`
runs. The fixture is not decorative -- every shape in it is one that has
already gone wrong, the nested register offset
`REG_FUART_RX_GPIO_MODE+(u32PadID-PAD_FUART_RX)` included: a lazy regex stops
at its inner `)` and yields an expression that will not evaluate, and a
register dropped that way shrinks a pad's MISC set until the pad stops being
checked at all.

Ingenic parts do not share a source, and the three kinds are not equally good:

| kind | source | names | covers |
|---|---|---|---|
| `spec` (T31) | the GPIO spec's port summary tables | per **wire** -- `UART1_RXD` | every pad the package brings out |
| `dt` (T40) | `boot/dts/ingenic/<soc>-pinctrl.dtsi` | per **device and port** -- `UART0_PC` | every routing the devicetree describes |
| `platform` (T21, T23) | `soc-<x>/include/mach/platform.h` | per **device** -- `UART0_PORTB` | only what the board file wires up |

Only T31 has a GPIO spec; only T40 has a pinctrl devicetree. `T40GPIO.xlsx`,
which sits beside the T40 SDK and looks like the T31 spec, is a net list for
one reference board and is not a source for this.

Both take `--verify <header>` to re-derive and diff without writing, so a
maintainer with the sources can prove the checked-in file still matches. Both
refuse rather than guess: a value with bits outside its field, a pad the SoC's
`gpio.h` does not name, a mode with two different register tuples, a row under
the wrong port's table.

CI has neither an SDK nor a datasheet and does not try to regenerate. What
stands in for it is `src/reginfo_test.c`, which sweeps every compiled-in
family for structural invariants and then sets every function of every pad and
reads it back, against a fabricated register file installed through the
`padmux_io_t` seam in `src/padmux.h`. Breaking one of Ingenic's four writes
fails 84 checks.

The generated headers are in `.clang-format-hook-exclude`, because reformatting
them would make `--verify` disagree with the generator forever after. That file
is read by the pre-commit hook and **not** by `scripts/apply-format` run by
hand, which is how a regeneration recipe once ended up wrapped mid-option and
no longer runnable. Use `scripts/format-changed`, which passes the exclusions
in.

## What is not covered

- **SigmaStar's MISC pads.** `PAD_PM_GPIO4` and the SAR, ETH and USB pads need
  several banks and a `0xBABE` PM unlock, and the vendor's own driver handles
  them in a hand-written switch rather than from the table. Three infinity6c
  modes that name a different register on each of those pads are dropped by
  the generator and named in the header, as are the six infinity6e ETH and USB
  pads whose rows describe `PAD_SPI_HLD`. Reading these would mean teaching
  the backend ALBANY and UTMI0; nothing needs it yet.
- **Ingenic pads the package does not bring out** are absent from the table
  rather than present and empty.
- **Other Ingenic parts.** T21, T23, T31 and T40 have tables. T10, T20, T30 and
  T41 do not, and would each need a source of one of the three kinds above --
  which, for the parts checked so far, does not exist in the vendor releases.
- **Hardware.** Every claim here is verified against vendor source and on a
  host. On top of that, HiSilicon, SigmaStar infinity6/6b0/6c, and Ingenic
  T21, T23, T31 and SigmaStar infinity6e have been run on real cameras and
  checked against an independent decode of their live registers. Ingenic T40
  has not: nobody has had one.

## Adding a family

1. Find the vendor's tables and check which of the three mechanisms it is.
   Getting this wrong is how both non-HiSilicon tables came to be GPIO
   controller registers labelled as pin-mux.
2. Either add a `MUXCTRL` table and an arm in `regs_by_chip()`, or write a
   backend: a `padmux_ops_t` with `walk`, `get` and `set`, plus an arm in
   `padmux_ops()` in `src/padmux.c`.
3. Add the token to `IPCHW_PADMUX_FAMILIES` in `CMakeLists.txt`. Vendor HALs
   and pad tables are separate knobs on purpose -- one answers "can this build
   detect the SoC", the other "does it carry the SoC's pad table".
4. Add the generation to the `chips[]` sweep in `src/reginfo_test.c`. The
   round trip there is the only proof the data is self-consistent until
   someone has the board.
