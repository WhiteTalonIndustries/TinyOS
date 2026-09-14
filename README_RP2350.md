# TinyOS on RP2350 (Pimoroni Pico Plus 2 W)

A second, independent port of TinyOS targeting the **Pimoroni Pico Plus 2 W**
(RP2350, 16MB W25Q128JV flash, 520KB SRAM, plus 8MB APS6404L PSRAM not yet
used), with a Waveshare 2.8" ResTouch LCD+touch+microSD board attached.

This is a from-scratch register-level port, same philosophy as the RP2040
build in [`src_2040/`](src_2040/) — no `pico-sdk`, no HAL. See
[README.md](README.md) for that build; **the RP2040 kernel there is
untouched by this port** and the two live side by side in this repo.

RP2350 is not register-compatible with RP2040 — different boot mechanism,
address map, and a Cortex-M33 core instead of M0+ — so nothing here is a
straight copy of `src_2040/`; each piece is re-derived against the [RP2350
datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf).

## Status: 2040-parity code complete, blocked on the same USB enumeration bug

Stage 1 (minimal boot, proven by blinking the attached LCD board's backlight
pin) is done and hardware-confirmed. Stage 2 (USB CDC-ACM console) and Stage 3
(flash/filesystem/editor/script/ADC, porting `src_2040/flash.c`, `fs.c`,
`editor.c`, `script.c`, `adc.c`) are now both **code-complete** — `src_rp2350/`
has the same command surface as the RP2040 build (`help`, `sysinfo`, `hello`,
`clear`, `ls`, `cat`, `write`, `mkdir`, `rm`, `mv`, `nano`, `run`, `format`,
`exit`) and builds clean with no warnings. This was deliberately written and
linked *ahead of* proving USB works, on the reasoning that none of it can be
exercised until the enumeration bug below is fixed anyway, so there's no
reason to block writing it. **It is therefore still functionally untested on
hardware** — first real test is whatever happens once a shell prompt is
reachable at all.

**Update, hardware-confirmed**: the clk_sys/clk_ref freeze described below is
now **fixed** — flashed and tested on real hardware. The board no longer
freezes: `clocks_init()` and `usb_device_init()` both complete, and it
reaches the "waiting for host enumeration" loop and stays there (confirmed
via the heartbeat blink, not a frozen LED). Both `clk_sys` (~125MHz) and
`clk_usb` (~48MHz) were independently verified correct using the RP2350
frequency counter (`check_khz()` in `usb.c`, 1 slow blink = confirmed
in-tolerance) — so this is not a clock-speed problem.

**Still open**: the host (tested on macOS) never sees the device at all —
`ls /dev/cu.usbmodem*` shows nothing, no matter how long it waits, and
`saw_bus_reset` (a flag set only when the host actually resets the bus, the
very first thing a host does on noticing a device) never goes true. This is
despite `USB_SIE_STATUS`'s `VBUS_DETECTED` and `CONNECTED` bits both reading
good. One real bug was found and fixed along the way — the device descriptor
had `idVendor = 0x0000`, an invalid VID that some host USB stacks (macOS
included) are known to silently refuse to enumerate — but fixing it did not
resolve enumeration by itself. A speculative, unproven `TRANSCEIVER_PD`
toggle in `usb_device_init()` (absent from every reference implementation
checked) was also removed. **Not yet resolved.** A debug probe (still out of
stock) or a USB protocol analyzer would help a lot here, since the remaining
symptom (host doesn't even acknowledge the device electrically) isn't very
LED-blink-diagnosable — see the "LCD debugging detour" section below for a
related, deeper finding (VREG voltage) discovered while chasing a *different*
bug, which has not yet been tried against this USB issue specifically and is
worth trying next.

### Known issue: clk_sys/clk_ref glitchless switch locks up the chip (fix found, untested)

An earlier version of `clocks_init()` found that clearing `CLK_SYS_CTRL`'s
glitchless `SRC` bit froze the chip solid, and worked around it by never
touching `SRC` at all — instead repointing `AUXSRC` while `SRC` stayed
parked on aux the whole time. That workaround got further but still hit a
second, unexplained hang before USB enumeration completed.

Comparing against a **working bare-pico-sdk USB example built for this exact
board family** (Pico 2 W / RP2350 — a TinyUSB CDC example found via
[this article](https://embeddedjourneys.com/blog/first-time-usb-data-stream-on-pico/),
source at [debocklabs/embeddedjourneys-labs](https://github.com/debocklabs/embeddedjourneys-labs)),
and reading pico-sdk's actual clock bring-up
(`pico_runtime_init/runtime_init_clocks.c`) directly, found the real bug:
the old workaround's "leave SRC on aux, just repoint AUXSRC" was itself the
unsafe move — that's exactly the "changing which source feeds an
actively-selected mux" hazard pico-sdk's own source comments warn about,
just applied to `AUXSRC` instead of `SRC`. Clearing `SRC` per se was never
the problem; pico-sdk's real boot sequence clears it too. What matters is
*when*: pico-sdk switches `clk_sys`/`clk_ref` away from aux to glitchless
**direct** sources (clk_ref-direct and ROSC respectively) *before* the PLLs
are even touched, while both are still cheap-and-safely clocked from the
always-on ROSC — then brings up the PLLs, then glitchlessly switches `SRC`
back to aux with `AUXSRC` already pointed at the now-locked PLL. The
original buggy attempt likely tried this same clear-`SRC` step but in the
wrong place in the sequence (e.g. after the PLLs were already reconfigured),
which would explain why it looked like clearing `SRC` itself was fatal.

`usb.c`'s `clocks_init()` now follows pico-sdk's proven order exactly:
disable resus → `xosc_init()` → switch `clk_sys`/`clk_ref` to direct sources
→ `pll_init()` both PLLs → switch `clk_ref` to XOSC → switch `clk_sys` to
aux/PLL_SYS → non-glitchless `clk_usb`/`clk_adc` bring-up (unchanged, this
part was never the problem).

**Confirmed fixed on real hardware** (flashed and tested): no more freeze,
and `check_khz()` (using the RP2350 frequency counter) verified both
`clk_sys` and `clk_usb` land in-tolerance of their targets. See "Status"
above for what's still open (USB enumeration itself).

### Why the backlight, not an LED

The Pico Plus 2 W's onboard LED is wired to the CYW43439 WiFi chip
(`LED_WLGP0` in Pimoroni's schematic), not to a plain RP2350 GPIO — driving
it needs a full WiFi-chip SPI/PIO driver. The Waveshare display is already
attached and its backlight (GPIO13) is a plain GPIO, so it's used instead as
a free, visible "did this boot" signal.

### What's different from the RP2040 build (and why)

- **No `boot2.S` equivalent.** RP2040 needs a 256-byte second-stage
  bootloader at flash offset 0 to configure QSPI before the boot ROM will
  jump into your code. RP2350's boot ROM instead scans the first 4KB of
  flash for a metadata block ("IMAGE_DEF", RP2350 datasheet §5.9) and
  configures flash access itself once it finds one — see
  [`picobin_block.s`](src_rp2350/picobin_block.s) for the minimum valid
  block (one `IMAGE_TYPE` item + a `LAST` item, 20 bytes, placed right after
  the vector table by [`linker_rp2350.ld`](src_rp2350/linker_rp2350.ld)).
- **A real Cortex-M33 vector table.** M0+ has no MemManage/BusFault/
  UsageFault handlers (those slots are just reserved words); M33 requires
  real entries there, plus a SecureFault slot M0+ doesn't have at all. See
  [`crt0_rp2350.s`](src_rp2350/crt0_rp2350.s) for the full table, including
  every IRQ 0–51 from datasheet table 95 (only slot 14, `USBCTRL_IRQ`, is
  wired to anything yet — the rest are placeholders for later stages).
- **Every peripheral base address moved.** e.g. `RESETS_BASE` is
  `0x4000c000` on RP2040, `0x40020000` on RP2350; `IO_BANK0_BASE` moved from
  `0x40014000` to `0x40028000`; reset-bit positions shifted too (`IO_BANK0`
  is reset bit 6 here, not bit 5). Re-derived from the datasheet per
  peripheral as each stage needs it — don't assume an RP2040 address/bit
  carries over.
- **A new "pad isolation" gotcha that doesn't exist on RP2040**: RP2350 pads
  reset with their `ISO` bit set, which isolates the pad from chip logic for
  low-power domain switching. Until software clears it, the pad won't drive
  (or, for an ADC input, won't sense) *no matter what else is configured* —
  see [`usb.c`](src_rp2350/usb.c)'s `led_init()` and
  [`adc.c`](src_rp2350/adc.c)'s `adc_init()` for two examples of clearing it.
- **`src_2040/stdint.h` and `string.h` are reused as-is** (via `-Isrc_2040`
  in the build command) — they're chip-agnostic freestanding typedefs with
  nothing RP2040-specific in them, so there was no reason to fork them.
- **USB's register offsets and DPRAM layout are identical to RP2040** —
  same IP block, confirmed against the datasheet. Only base addresses, some
  `RESETS`/`CLOCKS` offsets (RP2350 inserted an HSTX clock ahead of
  `CLK_USB`/`CLK_ADC` in the clocks block, shifting those and the frequency
  counter registers down), and one new requirement moved: `MAIN_CTRL`
  resets with `PHY_ISO` set, isolating the USB PHY until cleared — the same
  isolation pattern as the pad `ISO` bit, just for the USB analog block
  instead of a GPIO pad. See [`usb.c`](src_rp2350/usb.c)'s file header
  comment for the full list.
- **The boot ROM's flash-helper lookup mechanism changed, even though the
  helper functions themselves didn't.** RP2040 finds a lookup function via a
  fixed pointer at ROM address `0x18`, which takes a `(table, code)` pair
  (the table found via another fixed pointer at `0x14`). RP2350 (running Arm
  Secure, as this image does) finds its lookup function at a *different*
  fixed address, `0x16`, and calls it as `(code, flags)` directly — no
  separate table. The two-character function codes (`'RE'`, `'RP'`, `'IF'`,
  etc.) are unchanged. Confirmed against pico-sdk's
  `pico_bootrom/include/pico/bootrom.h` and
  `boot_bootrom_headers/include/boot/bootrom_constants.h`; see
  [`flash.c`](src_rp2350/flash.c)'s file header comment.
- **This board's ADC-capable GPIOs moved from 26-29 to 40-47.** The Pico
  Plus 2 W uses the QFN80 package (RP2350B, confirmed via `picotool info -d`
  — see rp2350_hardware_facts memory note), which has 48 GPIOs instead of
  RP2350A/RP2040's 30, and puts its 8 ADC channels on GPIO40-47 instead of
  GPIO26-29. `ADC_CS`'s `AINSEL` field widened from 3 bits to 4 to address
  them (channel 8 = temp sensor, not 4). See
  [`adc.c`](src_rp2350/adc.c)'s file header comment.

### Build & flash

VS Code task **"Build & Flash RP2350 (Pico Plus 2 W)"**, or from the command
line:

```sh
mkdir -p build_rp2350 release_rp2350
arm-none-eabi-gcc -c -mcpu=cortex-m33 -mthumb src_rp2350/crt0_rp2350.s -o build_rp2350/crt0_rp2350.o
arm-none-eabi-gcc -c -mcpu=cortex-m33 -mthumb src_rp2350/picobin_block.s -o build_rp2350/picobin_block.o
for f in main usb flash fs editor script adc lcd; do
  arm-none-eabi-gcc -c -mcpu=cortex-m33 -mthumb -O2 -ffreestanding -nostdinc -Isrc_2040 src_rp2350/$f.c -o build_rp2350/$f.o
done
arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -T src_rp2350/linker_rp2350.ld \
  build_rp2350/crt0_rp2350.o build_rp2350/picobin_block.o build_rp2350/main.o build_rp2350/usb.o \
  build_rp2350/flash.o build_rp2350/fs.o build_rp2350/editor.o build_rp2350/script.o build_rp2350/adc.o \
  build_rp2350/lcd.o -o build_rp2350/kernel_rp2350.elf -nostdlib -lgcc
arm-none-eabi-objcopy -O binary build_rp2350/kernel_rp2350.elf release_rp2350/kernel_rp2350.bin
picotool load release_rp2350/kernel_rp2350.bin -t bin -o 0x10000000 -v -f -x
```

Put the board in BOOTSEL mode first (hold **BOOTSEL**, plug in USB, release)
— same as the RP2040 build. Unlike the RP2040 persistent-boot path, there's
no separate checksum/padding step: RP2350's boot ROM validates the metadata
block itself, so the linked `.elf` → `.bin` → `picotool load` pipeline is
one step shorter.

**Current actual result** (see "Known issue" above — this is not yet the
intended working state): the backlight runs through several `led_blink()`
checkpoint groups (see the numbered comments in `usb.c`'s `clocks_init()`/
`console_init()`/`usb_device_init()` for what each count means — the
numbering has gaps and isn't in strict order after several rounds of live
debugging, but each count is still unique enough to tell how far execution
got) and then goes solid, without the board ever enumerating on the host
(check with `ls /dev/cu.usbmodem*` on macOS — nothing appears). A debug probe
(e.g. Raspberry Pi Debug Probe, ~$12) wired via SWD, then `openocd -f
interface/cmsis-dap.cfg -f target/rp2350.cfg` + gdb to halt the CPU and read
`$pc`/registers at the point of the freeze, would resolve this far faster
than blink-code bisection — but the Debug Probe is currently out of stock
with no known restock date, so that path is on hold. In the meantime, this
board is confirmed A4 stepping silicon (vs. the A2 stepping some of
pico-sdk's own RP2350 errata workarounds target — see e.g.
`PICO_BOOTROM_WORKAROUND_RP2350_A2_ACTIVITY_LED_BUG` in pico-sdk's
`bootrom.h`), which is worth keeping in mind if resuming this by comparing
against A2-era errata notes: some may not apply to this chip.

### LCD debugging detour (2026-09-14): a register-level driver, several real bugs found, one still open

Separately from USB, a from-scratch ST7789 driver for the attached Waveshare
2.8" display (`src_rp2350/lcd.c`/`lcd.h`) was written purely as a *debugging
tool* — the plan was to show register dumps as blocky digits on screen
instead of counting backlight blinks. It doesn't work yet (screen stays
black), but the debugging process along the way found several real,
confirmed bugs worth recording, plus one still-open, genuinely strange
finding that may matter for USB too.

**Confirmed real bugs, fixed:**
- `pad_setup()` did a full-register overwrite of `PADS_BANK0`, forcing
  `IE` (input enable) to 0. pico-sdk's `gpio_set_function()` always sets
  `IE=1`, even for pure-output pins — confirmed by literally dumping the
  register (`resources/c/regdump.c`, a diagnostic pico-sdk build that prints
  over USB serial) after a known-working `LCD_Init()`: every pin read back
  `PAD=0x56`, not the `0x00`-ish value a naive reset-defaults assumption
  would suggest. Fixed with a proper read-modify-write that only touches
  `IE`/`OD`/`ISO`, leaving `SCHMITT`/`PDE`/`PUE`/`DRIVE` at whatever the true
  silicon reset default is (matching pico-sdk's approach exactly), rather
  than forcing them to 0.
- The LCD, touch controller, and microSD slot all share one SPI1 bus
  (separate chip-selects). The driver never deselected `TP_CS`/`SD_CS`,
  unlike Waveshare's reference `DEV_GPIO_Init()` which explicitly drives
  every chip-select on the bus high before touching anything — a real gap,
  now fixed.

**Confirmed by a side-by-side hardware test**: a fresh pico-sdk build of
Waveshare's own demo (`resources/c/build_pico2/main.uf2`, built after fixing
two unrelated pre-existing missing-`#include <stdint.h>` bugs in their
SD/FatFs code) lights up the display perfectly on this exact board/cable —
so the panel, wiring, and seating are all good. Running TinyOS's own
unmodified `lcd.c` *inside that same working pico-sdk environment*
(`resources/c/tinyos_lcd_test.c` + `tinyos_lcd.c`) also worked (solid red
fill) — proving the driver's *logic* is correct and the bug is specifically
something about **TinyOS's own boot environment**, not the LCD code.

**Found via that comparison**: RP2350's core voltage regulator (VREG) powers
up at 1.10V, but `SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST` defaults to **1** for
RP2350 in pico-sdk (unlike RP2040, where it's 0) — every normal pico-sdk
RP2350 build bumps VREG to 1.15V during boot regardless of target clock
speed. TinyOS never touched VREG at all. This is a real, confirmed
RP2350-specific requirement (see `vreg_bump_voltage()`, added to
`usb.c`'s `clocks_init()`, and the `rp2350_gotchas` memory note) — but
**adding it to TinyOS did not fix the LCD**, so either it wasn't the (whole)
explanation for the LCD symptom, or something else is still missing too.

**Still open, genuinely strange**: overriding pico-sdk's `runtime_init_clocks()`
weak symbol with TinyOS's own clock-init logic — even calling pico-sdk's
*own real library functions* verbatim, not our reimplementation — hangs
somewhere before `main()` is ever reached, in an otherwise fully-working
pico-sdk environment. A pure-GPIO-blink override (zero clock code) was
proven to execute reliably in the same slot, ruling out the override
mechanism itself. This means clock bring-up behaves differently depending on
*where* it's invoked from, for reasons not yet understood — an unresolved,
narrower mystery than where this detour started, and possibly related to
whatever's still blocking USB enumeration above, since both are
clock/bring-up-adjacent. Worth a fresh look with a debug probe if one
becomes available; see `resources/c/tinyos_clocks_override.c` for the exact
bisection state this was left in.

**Practical takeaway for next time**: the `resources/c/regdump.c` and
`resources/c/tinyos_lcd_test.c` techniques (dump real hardware state from a
known-working pico-sdk build; run TinyOS's own code inside a known-working
environment) were far more productive than blink-code guessing once USB
serial was available as an output channel, and are reusable for the open
USB mystery too, not just LCD.

### Next stages

The 2040-parity work (flash/filesystem/editor/script/ADC, all ported and
building — see "Status" above) is done pending USB actually working. Once
enumeration is fixed, verify each ported command end-to-end (`ls`, `cat`,
`write`, `nano`, `run`, ADC-backed script randomness) before moving on. Then:
SPI1 + microSD driver (the actual goal of this whole port). LCD pixel output
and a real FAT layer on top of the SD block driver are explicitly out of
scope until the SD driver itself is proven.
