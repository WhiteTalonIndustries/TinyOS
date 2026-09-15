# TinyOS on RP2350 (Pimoroni Pico Plus 2 W)

A second target for TinyOS: the **Pimoroni Pico Plus 2 W** (RP2350B/QFN80,
16MB W25Q128JV flash, 520KB SRAM, 8MB APS6404L PSRAM not yet used), with a
Waveshare 2.8" ResTouch LCD+touch+microSD board attached. See
[README.md](README.md) for the original RP2040 build; **`src_2040/` is
untouched by this port**.

## This port is built on pico-sdk, not bare-metal

The RP2040 build is a from-scratch register-level port with zero
`pico-sdk`/HAL. An earlier version of this RP2350 port tried the same
approach, and got a minimal boot + backlight-blink working, but repeatedly
hit real, hard-to-diagnose hangs in clock bring-up that turned out to depend
on boot history (code that worked reliably across many warm reflash-and-reset
cycles hung immediately after a genuine cold power-cycle — traced to a
missing broad early-peripheral-unreset step and an XOSC startup-delay value
that was too short for a truly cold crystal, among other things found along
the way; see git history around commit `502e9d1` and the `rp2350_gotchas`
memory note for the full, fairly long story). After enough of these, the
decision was made to stop re-deriving RP2350's boot/clock/SPI sequence from
scratch and instead build this port **on pico-sdk**, reusing Waveshare's own
proven `DEV_Config.c`/`LCD_Driver.c` (see
[resources/c/examples/lcd_test.c](resources/c/examples/lcd_test.c) for
Waveshare's original reference) directly rather than a bare-metal
reimplementation of the same logic.

**This is a deliberate, explicit departure from the no-SDK philosophy for
this specific board**, made after the bare-metal approach cost far more time
than it saved. `src_2040/`'s approach is unaffected.

## Status: 2040-parity shell, filesystem, and script runner all working

`src_rp2350/` is now a normal pico-sdk CMake project, and **the board must
be built as `pimoroni_pico_plus2_w_rp2350`, not `pico2_w`.** They differ in
`PICO_RP2350A` (0 vs 1) among other things (flash/PSRAM size, CYW43 pins) --
building against `pico2_w` silently resolves ADC to the wrong GPIOs
(26-29 instead of 40-47) since this board is the QFN80/RP2350B package, not
the QFN60/RP2350A the plain Pico 2 W uses. Confirmed via `picotool info -d`
and pico-sdk's own `src/boards/include/boards/pimoroni_pico_plus2_w_rp2350.h`.

- `lib/config/DEV_Config.c` + `lib/lcd/LCD_Driver.c` — Waveshare's actual
  driver code, copied in as-is (touch/GUI/Bmp/fatfs/sdcard intentionally
  excluded — this is a debugging-output display, not a full graphics stack).
- `status.c`/`status.h` — solid-color status codes (`STATUS_BOOTING`/
  `STATUS_LCD_OK`/`STATUS_USB_WAITING`/`STATUS_USB_OK`/`STATUS_ERROR`).
  **All five confirmed rendering correctly on hardware.**
- `usb.c`/`usb.h` — a `console_*` API (matching `src_2040/usb.h`'s
  interface) implemented on top of pico-sdk's `stdio_usb` (TinyUSB
  underneath), so `editor.c`/`script.c` below can be reused from
  `src_2040` completely unmodified.
- `flash.c` — `hardware_flash`-backed (`flash_range_erase`/`_program` with
  `save_and_disable_interrupts()`, since this is single-core with no
  `multicore_launch_core1()`) implementation of `src_2040/flash.h`'s
  interface.
- `fs.c`, `editor.c`, `script.c` — copied verbatim from `src_2040` (pure
  logic, no register access; `fs.c` only has its flash-size constant
  changed for this board's 16MB part).
- `adc.c` — `hardware_adc`-backed, using `ADC_BASE_PIN`/`NUM_ADC_CHANNELS`
  (which resolve correctly to GPIO40-47 + temp sensor *only* when built
  against the correct board, see above). Named `tinyos_adc_init()`, not
  `adc_init()`, since pico-sdk's own `hardware/adc.h` already declares a
  function with that exact name.
- `main.c` — full `src_2040`-parity shell: `help`, `sysinfo`, `hello`,
  `clear`, `ls`, `cat`, `write`, `mkdir`, `rm`, `mv`, `nano`, `run`,
  `format`, `exit`.

**Hardware-confirmed working, end to end, over a real USB connection**:
enumeration, banner, `sysinfo`, `format`, `write`/`ls`/`cat`/`mkdir`/`rm`
(real flash-backed persistence), and `run` executing a TinyScript script
that calls `randdigit()` — confirmed pulling genuinely different values
from the ADC noise source across repeated runs. `nano` (the full-screen
editor) hasn't been separately exercised yet (harder to script
non-interactively) but shares the same `fs_read`/`fs_write` calls already
proven above and is otherwise identical to `src_2040/editor.c`.

- `lcd_console.c`/`.h` — mirrors the entire USB shell (banner, prompts,
  command output, echoed input) onto the LCD as scrolling text, via a
  registered pico-sdk `stdio_driver_t` (so it catches every `printf`/
  `putchar` call anywhere in the codebase, not just calls that happen to go
  through `usb.c`'s `console_*` wrappers) plus Waveshare's `LCD_GUI.c`
  (`GUI_DisChar`, Font16 only). **Confirmed showing readable shell text on
  hardware.** No real scrolling yet (clears and restarts at the top when
  the grid fills) and no ANSI emulation beyond swallowing the two escape
  sequences this codebase actually emits (`\033[2J`/`\033[H`, both treated
  as "clear").

The microSD slot on the Waveshare board is also available as a persistence
mechanism across reflashes if needed later (e.g. a boot log written to SD
and read back after a hang) — noted here since it's easy to forget it's
there once SD/FatFs work starts.

### Build & flash

```sh
export PICO_SDK_PATH=/Users/robert/pico-sdk   # wherever pico-sdk lives
export PATH="/Applications/ArmGNUToolchain/15.3.rel1/arm-none-eabi/bin:$PATH"
cd src_rp2350
mkdir -p build && cd build
cmake -DPICO_BOARD=pimoroni_pico_plus2_w_rp2350 -DPICO_SDK_PATH=$PICO_SDK_PATH ..
cmake --build . --target tinyos_rp2350 -j8
# Board already running a pico-sdk USB build? No need to touch BOOTSEL:
picotool reboot -f -u
# Otherwise: hold BOOTSEL, plug in USB, release -- then:
picotool load tinyos_rp2350.uf2 -v -f -x
```

### Next steps

1. SPI1 + microSD driver is the actual goal of this whole port — Waveshare's
   own `lib/sdcard`/`lib/fatfs` (in `resources/c/`) are the obvious starting
   point. The SD slot can also double as a persistence mechanism for
   debugging (see above) once it's up.
2. Once SD works, a real filesystem (FatFs) could replace/augment the
   current flat TinyFS layer if useful.

### What's in `resources/c/` and `scratch/`

`resources/c/` is Waveshare's original example project (LCD + touch + SD +
FatFs demo), kept as **pristine reference** — see `examples/lcd_test.c` for
their reference `main()`. Two pre-existing bugs (missing
`#include <stdint.h>` in `lib/fatfs/fatfs_storage.h` and
`lib/sdcard/MMC_SD.h`) were fixed there so it actually builds; otherwise
don't add throwaway/diagnostic files here.

`scratch/rp2350_bare_metal_debug/` holds the diagnostic pico-sdk harnesses
(`regdump.c`, `tinyos_lcd_test.c`, `tinyos_clocks_override.c`) built during
the bare-metal debugging session — no longer needed now that the port
builds on pico-sdk, kept only as a record of how the decision to switch was
reached. New throwaway test files should go in `scratch/`, not scattered
into `resources/c/` or elsewhere.
