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

## Status: display working, minimal

`src_rp2350/` is now a normal pico-sdk CMake project:

- `lib/config/DEV_Config.c` + `lib/lcd/LCD_Driver.c` — Waveshare's actual
  driver code, copied in as-is (touch/GUI/Bmp/fatfs/sdcard intentionally
  excluded — this is a debugging-output display, not a full graphics stack).
- `main.c` — brings up the display and fills it solid red.
  **Hardware-confirmed working**, cold boot included.

### Build & flash

```sh
export PICO_SDK_PATH=/Users/robert/pico-sdk   # wherever pico-sdk lives
export PATH="/Applications/ArmGNUToolchain/15.3.rel1/arm-none-eabi/bin:$PATH"
cd src_rp2350
mkdir -p build && cd build
cmake -DPICO_BOARD=pico2_w -DPICO_SDK_PATH=$PICO_SDK_PATH ..
cmake --build . --target tinyos_rp2350 -j8
# Put the board in BOOTSEL mode (hold BOOTSEL, plug in USB, release), then:
picotool load tinyos_rp2350.uf2 -v -f -x
```

### Next steps

1. Turn the solid-color test into a small set of status/error codes (a few
   distinct solid colors first, since that's already proven; richer
   digit/text output can follow once that's solid) — the original goal of
   building the display up as a debugging aid for other subsystems.
2. Bring up USB (CDC-ACM console) the same way: reuse pico-sdk/TinyUSB
   rather than re-deriving USB device-controller register sequences
   bare-metal, given how the clock bring-up went. `pico_enable_stdio_usb`
   is already wired up in `CMakeLists.txt` (unused so far — `main.c`
   doesn't call `stdio_init_all()` yet).
3. Once there's a working console, revisit the shell/filesystem/flash
   layer — likely also worth building on pico-sdk (`hardware_flash`,
   `pico_flash`) rather than the bare-metal boot-ROM-lookup approach tried
   earlier, given the same reliability lesson.
4. SPI1 + microSD driver is still the actual goal of this whole port —
   Waveshare's own `lib/sdcard`/`lib/fatfs` (in `resources/c/`) are the
   obvious starting point once the above is solid.

### What's in `resources/c/`

Waveshare's original example project (LCD + touch + SD + FatFs demo),
kept as reference and as a source to copy proven-working code from — see
`examples/lcd_test.c` for their reference `main()`. Two pre-existing bugs
(missing `#include <stdint.h>` in `lib/fatfs/fatfs_storage.h` and
`lib/sdcard/MMC_SD.h`) were fixed there so it actually builds; otherwise
it's unmodified. `resources/c/regdump.c`, `tinyos_lcd_test.c`, and
`tinyos_clocks_override.c` are diagnostic harnesses from the bare-metal
debugging session — no longer needed now that the port builds on pico-sdk,
kept only as a record of how the decision to switch was reached.
