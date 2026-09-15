#ifndef TINYOS_USB_H
#define TINYOS_USB_H

/* Console API shim backed by pico-sdk's stdio_usb -- lets src_2040's
 * editor.c/script.c be reused verbatim (they only depend on this API, not
 * on how the console is actually implemented). Real USB bring-up happens
 * via stdio_init_all() in main.c; these just wrap printf/getchar. */

void console_putc(char c);
void console_puts(const char *s);
void console_write(const char *buf, unsigned int len);
char console_getc(void);
int console_has_input(void);

/* Halts the CPU. Does not return. */
void console_disconnect(void);

#endif
