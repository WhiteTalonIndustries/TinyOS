#ifndef TINYOS_TUSB_CONFIG_H
#define TINYOS_TUSB_CONFIG_H

/* Composite CDC (shell console) + MSC (SD card, "mount"/"unmount") device.
 * CFG_TUSB_MCU/CFG_TUSB_OS come from the tinyusb_device target's own
 * compile definitions (same ones pico_stdio_usb relies on) -- see
 * pico-sdk's lib/tinyusb/hw/bsp/rp2040/family.cmake. Modeled directly on
 * lib/tinyusb/examples/device/cdc_msc/src/tusb_config.h, trimmed to only
 * what this board needs (full-speed only, single CDC + single MSC LUN). */

#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT 0
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_NONE
#endif

#define CFG_TUD_ENABLED   1
#define CFG_TUD_MAX_SPEED OPT_MODE_FULL_SPEED

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#endif

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE 64
#endif

#define CFG_TUD_CDC     1
#define CFG_TUD_MSC     1
#define CFG_TUD_HID     0
#define CFG_TUD_MIDI    0
#define CFG_TUD_VENDOR  0

#define CFG_TUD_CDC_RX_BUFSIZE  256
#define CFG_TUD_CDC_TX_BUFSIZE  256
#define CFG_TUD_CDC_EP_BUFSIZE  64

/* Exactly one sector per transfer -- msc_disk.c's read10/write10 callbacks
 * assume offset==0 and bufsize==512 on every call. */
#define CFG_TUD_MSC_EP_BUFSIZE  512

#endif
