#ifndef TINYOS_USB_H
#define TINYOS_USB_H

/* Brings up clocks + the RP2040 USB device controller as a USB CDC-ACM
 * (virtual serial port) device, and blocks until the host has finished
 * enumerating it. */
void console_init(void);

void console_putc(char c);
void console_puts(const char *s);
void console_write(const char *buf, unsigned int len);
char console_getc(void);
int console_has_input(void);

/* Soft-disconnects from the USB host and halts the CPU. Does not return. */
void console_disconnect(void);

#endif
