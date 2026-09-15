#include "usb.h"
#include "pico/stdlib.h"
#include <stdio.h>

/* Mirroring to the LCD happens via a registered stdio_driver_t
 * (lcd_console.c) that hooks pico-sdk's stdout fan-out directly -- it
 * catches everything written through putchar()/printf() here, main.c's
 * printf() calls, etc. all in one place, so console_putc() doesn't need
 * its own separate mirror call (that would double-render every character:
 * once here, once via the driver intercepting this same putchar()). */
void console_putc(char c) {
    putchar(c);
    stdio_flush();
}

void console_puts(const char *s) {
    while (*s) {
        if (*s == '\n') console_putc('\r');
        console_putc(*s++);
    }
}

void console_write(const char *buf, unsigned int len) {
    unsigned int i;
    for (i = 0; i < len; i++) {
        if (buf[i] == '\n') console_putc('\r');
        console_putc(buf[i]);
    }
}

char console_getc(void) {
    int c;
    do {
        c = getchar();
    } while (c == PICO_ERROR_TIMEOUT);
    return (char)c;
}

int console_has_input(void) {
    /* CAVEAT: getchar_timeout_us() consumes a byte if one's available (no
     * true peek in pico-sdk's stdio) -- this is destructive, unlike the
     * bare-metal version's non-consuming peek. Currently unused by
     * editor.c/script.c/main.c, so harmless, but don't call this and then
     * expect console_getc() to still see that byte. */
    return getchar_timeout_us(0) != PICO_ERROR_TIMEOUT;
}

void console_disconnect(void) {
    while (1) {
        tight_loop_contents();
    }
}
