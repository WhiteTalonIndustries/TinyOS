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

## Status: Stage 2 — USB CDC-ACM console (in progress, blocked short of enumeration)

Stage 1 (minimal boot, proven by blinking the attached LCD board's backlight
pin) is done and hardware-confirmed. Stage 2's shell/console code
(`main.c`/`usb.c`) is written and matches the RP2040 build's UX (`help`,
`sysinfo`, `hello`, `clear`, `exit` — filesystem commands wait for Stage 3),
but **the board does not yet enumerate as a USB device**. Extensive hardware
debugging (see below and the `rp2350_gotchas`/`rp2350_port_status` memory
notes) found a real, reproducible RP2350 hazard along the way and worked
around it, but hit a second, later hang before reaching a working console.
Next session should resume with this open problem — a debug probe (SWD +
openocd/gdb) would resolve it far faster than further LED-blink diagnostics.

### Known issue: clk_sys/clk_ref glitchless switch locks up the chip

The standard clock bring-up sequence — glitchlessly switch `clk_sys`/
`clk_ref` away from their aux inputs before reconfiguring the PLLs that feed
those inputs, then switch back — is copied nearly verbatim from
`src_2040/usb.c` (which works fine on real RP2040 hardware) and matches the
pico-sdk's own `clocks.c` algorithm exactly (confirmed against the RP2350
datasheet's SDK source excerpt). On this RP2350 board, the moment
`CLK_SYS_CTRL`'s glitchless `SRC` bit is cleared (the very first write
`clocks_init()` makes), **the entire CPU freezes solid** — not a spin-loop
hang, a total halt: code before that write runs fine (proven via a
progressively-narrowed series of isolation builds — see git history / the
session that found this), and code after it, even in a completely different,
unrelated function called unconditionally right after, never executes again,
consistent with the switch stalling `clk_sys` itself (the CPU's own clock)
rather than just spinning in the software wait-loop.

Checked and ruled out as explanations: ACCESSCTRL (CLOCKS defaults to
Secure+Privileged+Core0 access, which is what our image runs as), ROSC
disabled (datasheet confirms ROSC defaults to ENABLE on power-up), and the
new `CLK_SYS_RESUS` feature (defaults to disabled). Root cause not yet
understood — this needs a debug probe to actually catch the CPU at the
moment of the freeze.

**Current workaround** (in `usb.c`'s `clocks_init()`): never touch
`CLK_SYS_CTRL`/`CLK_REF_CTRL`'s glitchless `SRC` bit at all. Bring up
`clk_usb` instead via the *non*-glitchless clock-configure path (disable,
reconfigure `AUXSRC`, re-enable — no risky "switch away first" step, since
only `clk_ref`/`clk_sys` have a glitchless mux per the SDK), which gets USB
an accurate 48MHz. Separately, `clk_sys` is sped up by repointing only its
`AUXSRC` sub-mux at `PLL_SYS` while leaving `SRC` itself untouched at its
default (`1` = aux) — this is exactly the kind of glitch pico-sdk's own
source comment warns against causing, tried anyway as a pragmatic bet since
it avoids the specific bit that reproducibly crashes the chip. This got
further (multiple checkpoints fire, unlike before) but the board still
doesn't enumerate, and eventually locks up again later — possibly inside
`usb_task()`, possibly a delayed consequence of the same glitch. **Not
resolved.**

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
  *no matter what else is configured* — see the comment in
  [`main.c`](src_rp2350/main.c)'s `bkl_init()`.
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

### Build & flash

VS Code task **"Build & Flash RP2350 (Pico Plus 2 W)"**, or from the command
line:

```sh
mkdir -p build_rp2350 release_rp2350
arm-none-eabi-gcc -c -mcpu=cortex-m33 -mthumb src_rp2350/crt0_rp2350.s -o build_rp2350/crt0_rp2350.o
arm-none-eabi-gcc -c -mcpu=cortex-m33 -mthumb src_rp2350/picobin_block.s -o build_rp2350/picobin_block.o
for f in main usb; do
  arm-none-eabi-gcc -c -mcpu=cortex-m33 -mthumb -O2 -ffreestanding -nostdinc -Isrc_2040 src_rp2350/$f.c -o build_rp2350/$f.o
done
arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -T src_rp2350/linker_rp2350.ld \
  build_rp2350/crt0_rp2350.o build_rp2350/picobin_block.o build_rp2350/main.o build_rp2350/usb.o \
  -o build_rp2350/kernel_rp2350.elf -nostdlib -lgcc
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
(check with `ls /dev/cu.usbmodem*` on macOS — nothing appears). If you pick
this back up: the fastest path forward is a debug probe (e.g. Raspberry Pi
Debug Probe, ~$12) wired via SWD, then `openocd -f interface/cmsis-dap.cfg -f
target/rp2350.cfg` + gdb to halt the CPU and read `$pc`/registers at the
point of the freeze, rather than continuing with blink-code bisection.

### Next stages

Flash/filesystem access (Stage 3) → SPI1 + microSD driver (Stage 4, the
actual goal of this phase). LCD pixel output and a real FAT layer on top of
the SD block driver are explicitly out of scope until the SD driver itself
is proven.
