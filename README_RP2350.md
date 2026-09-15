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

- `lib/config/DEV_Config.c` + `lib/lcd/LCD_Driver.c` + `lib/lcd/LCD_GUI.c`
  (`Font12`/`Font24` only) — Waveshare's actual driver code, copied in as-is
  (touch/Bmp intentionally excluded).
- `status.c`/`status.h` — "TinyOS" rendered as centered colored text
  (`STATUS_BOOTING`/`STATUS_LCD_OK`/`STATUS_USB_WAITING`/`STATUS_USB_OK`/
  `STATUS_ERROR`, each its own color) on a black background — a splash
  label, not a solid-color block. **Confirmed rendering correctly.**
- `lcd_console.c`/`.h` — mirrors the entire USB shell (banner, prompts,
  command output, echoed input) onto the LCD as scrolling Font12 text, via
  a registered pico-sdk `stdio_driver_t` (catches every `printf`/`putchar`
  call anywhere in the codebase). Display runs landscape, rotated 90deg
  clockwise (`LCD_Init(U2D_R2L, ...)` — `D2U_L2R` was tried first and
  confirmed on hardware to rotate the wrong way). **Confirmed showing
  readable shell text.** No real scrolling yet (clears and restarts at the
  top when the grid fills) and no ANSI emulation beyond swallowing the two
  escape sequences this codebase actually emits (`\033[2J`/`\033[H`, both
  treated as "clear").
- `usb.c`/`usb.h` — a `console_*` API (matching `src_2040/usb.h`'s
  interface) on top of a **composite TinyUSB CDC+MSC device** (not
  pico-sdk's own `pico_stdio_usb`, which bakes in a fixed CDC-only
  descriptor set that can't coexist with a second class -- see
  `usb_descriptors.c`, adapted directly from
  `lib/tinyusb/examples/device/cdc_msc/src/usb_descriptors.c`). Registers
  a `stdio_driver_t` for the CDC console, same fan-out mechanism
  `lcd_console.c` uses, so `editor.c`/`script.c`/every `printf()` call
  site needed zero changes.
- `msc_disk.c` — the MSC side of that composite device: exposes the SD
  card to the connected PC as a raw USB drive, reading/writing straight
  through to `MMC_SD.c`'s sector I/O (the same calls `diskio.c` uses for
  TinyOS's own filesystem). Gated by `fs_usb_mount()`/`fs_usb_unmount()`
  in `fs.c` so exactly one side (TinyOS's FatFs or the USB host) ever
  touches the card at a time; a host "safely eject" also triggers
  `fs_usb_unmount()` automatically. New shell commands `mount` (hand the
  card to the host) and `unmount` (take it back). **Confirmed working on
  hardware**: `mount` makes the card appear as a normal drive on the host
  PC (it's a real FAT filesystem, since it's the same medium `fs.c`
  formats), and `unmount` hands it back to the shell's own `ls`/`cat`/etc.
- `fs.c` — **microSD is TinyOS's user storage; the onboard 16MB flash is
  reserved for the system image only** (nothing touches raw flash anymore
  — `flash.c`/`flash.h` were removed entirely). A from-scratch
  implementation of `src_2040/fs.h`'s interface backed by Waveshare's
  `lib/sdcard/MMC_SD.c` (SPI driver) + the elm-chan FatFs library
  (`lib/fatfs/`, an older pre-`FF_`-prefix release — `_USE_MKFS` enabled in
  `ffconf.h` so `fs_format()` can do a real low-level SD format;
  `fatfs_storage.c`, Waveshare's bitmap-display-specific wrapper, is not
  used). Filenames are 8.3 short-name only (`_USE_LFN 0`), so they show up
  uppercase (`HELLO.TXT`, not `hello.txt`). `_FS_RPATH` is enabled (2) so
  `cd`/`pwd` (`fs_chdir()`/`fs_getcwd()`) are FatFs's own relative-path
  support, not hand-rolled path math — `ls` lists `.` (the current
  directory) rather than a hardcoded root. **Confirmed working on
  hardware**: `mkdir`, `cd` into it, `pwd` reflects the new path, `cd ..`
  back out.
- `editor.c`, `script.c` — copied verbatim from `src_2040` (pure logic, no
  register/storage access — work unchanged against the new SD-backed
  `fs.c` through the same `fs.h` interface).
- `adc.c` — `hardware_adc`-backed, using `ADC_BASE_PIN`/`NUM_ADC_CHANNELS`
  (which resolve correctly to GPIO40-47 + temp sensor *only* when built
  against the correct board, see above). Named `tinyos_adc_init()`, not
  `adc_init()`, since pico-sdk's own `hardware/adc.h` already declares a
  function with that exact name.
- `wifi.c`/`wifi.h` — CYW43439 WiFi + lwIP (`pico_cyw43_arch_lwip_poll`,
  `NO_SYS=1`, polled from `usb.c`'s idle loops alongside `tud_task()` --
  see `lwipopts.h`). Credentials come from `WIFI.CFG` on the SD card (two
  lines: SSID, then password), never baked into source or committed.
  **Stays off until explicitly requested** (`wifi connect`) -- calling
  `cyw43_arch_init()` unconditionally at boot caused a hard panic on this
  board; root cause not isolated, but gating it behind a shell command
  run after the rest of the system is already up is confirmed stable.
  `ifconfig` reports SSID + IP. `wifi disconnect` tears the chip back down
  (`cyw43_arch_deinit()`); a later `wifi connect` re-inits cleanly.
  **Confirmed working on hardware**: joins a real network and gets a DHCP
  lease.
- `net.c`/`net.h` — `ping <host>` (raw ICMP echo via lwIP's `raw` API) and
  `browser <host> [path]` ("super limited browser": plain HTTP/1.0 GET
  over lwIP's raw TCP API, no HTTPS/redirects/HTML rendering, just dumps
  the raw response to the console). Both accept a hostname (resolved via
  `dns_gethostbyname()`, polled synchronously -- there's no callback-based
  concurrency here, TinyOS's shell is single-threaded) or a dotted IP.
- `main.c` — full `src_2040`-parity shell plus `cd`/`pwd`/`wifi
  connect`/`wifi disconnect`/`ifconfig`/`ping`/`browser`: `help`,
  `sysinfo`, `hello`, `clear`, `ls`, `cat`, `write`, `mkdir`, `rm`, `mv`,
  `nano`, `run`, `cd`, `pwd`, `format`, `mount`, `unmount`, `wifi connect`,
  `wifi disconnect`, `ifconfig`, `ping`, `browser`, `exit`.

**Hardware-confirmed working, end to end, over a real USB connection, on a
real 16GB microSD card**: enumeration, banner, `sysinfo`, `format` (real
low-level SD format), `write`/`ls`/`cat`/`mkdir`/`rm` (a directory created
before a reflash was confirmed still present after, i.e. genuine on-card
persistence, not just in-RAM state), and `run` executing a TinyScript
script that calls `randdigit()` — confirmed pulling genuinely different
values from the ADC noise source across repeated runs. `nano` (the
full-screen editor) hasn't been separately exercised yet (harder to script
non-interactively) but shares the same `fs_read`/`fs_write` calls already
proven above and is otherwise identical to `src_2040/editor.c`.

### Build & flash

```sh
export PICO_SDK_PATH=/Users/robert/pico-sdk   # wherever pico-sdk lives
export PATH="/Applications/ArmGNUToolchain/15.3.rel1/arm-none-eabi/bin:$PATH"
cd src_rp2350
mkdir -p build && cd build
cmake -DPICO_BOARD=pimoroni_pico_plus2_w_rp2350 -DPICO_SDK_PATH=$PICO_SDK_PATH ..
cmake --build . --target tinyos_rp2350 -j8
# Hold BOOTSEL, plug in USB (or press reset with BOOTSEL held), release -- then:
picotool load tinyos_rp2350.uf2 -v -f -x
```

### Next steps

`ping` and `browser` haven't been exercised against a real remote host
yet (only built/flashed) -- worth confirming on hardware.

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
