#ifndef TINYOS_WIFI_H
#define TINYOS_WIFI_H

#include <stdbool.h>

/* Brings up the CYW43 WiFi chip + lwIP (NO_SYS/polling mode, see
 * lwipopts.h) and joins the network named in WIFI.CFG on the SD card
 * (two lines: SSID, then password -- plain text, since it lives on a
 * removable card the user controls, not in the git repo). Call once at
 * boot, after fs_init() (needs the SD card mounted to read the file).
 * Safe to call even with no SD card / no WIFI.CFG present -- just skips
 * WiFi bring-up and leaves wifi_is_connected() false. */
void wifi_init(void);

bool wifi_is_connected(void);

/* Pumps the cyw43/lwIP poll loop -- must be called regularly (this is
 * NO_SYS=1 lwIP, nothing runs it in the background). No-op if wifi_init()
 * never connected. Called from usb.c's idle/blocking-read loops, same
 * pattern as tud_task(). */
void wifi_poll(void);

/* Prints connection state (SSID + IP, or "not connected") for the
 * `ifconfig` shell command. */
void wifi_print_status(void);

#endif
