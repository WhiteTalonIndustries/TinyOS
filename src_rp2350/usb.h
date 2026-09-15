#ifndef TINYOS_USB_H
#define TINYOS_USB_H

#include <stdbool.h>

/* Console API shim backed by our own composite TinyUSB CDC+MSC device
 * (see usb.c, usb_descriptors.c, msc_disk.c) instead of pico-sdk's
 * pico_stdio_usb -- lets src_2040's editor.c/script.c be reused verbatim
 * (they only depend on this API, not on how the console is actually
 * implemented). */

/* Brings up the USB device stack and registers the CDC console with
 * pico-sdk's stdio. Call once at boot, after stdio_init_all(). */
void usb_init(void);

/* Services the USB stack and reports whether a host has the CDC port
 * open (DTR asserted) -- poll this instead of stdio_usb_connected(). */
bool usb_console_connected(void);

void console_putc(char c);
void console_puts(const char *s);
void console_write(const char *buf, unsigned int len);
char console_getc(void);
int console_has_input(void);

/* Halts the CPU. Does not return. */
void console_disconnect(void);

#endif
