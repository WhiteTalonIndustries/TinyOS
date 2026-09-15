#include "usb.h"
#include "tusb.h"
#include "pico/stdlib.h"
#include "pico/stdio/driver.h"
#include <stdio.h>

/* Replaces pico-sdk's own pico_stdio_usb (CDC-only) now that the device
 * needs to be composite (CDC console + MSC for "mount"/"unmount", see
 * msc_disk.c) -- TinyUSB only supports one device/config descriptor set
 * per build, and pico_stdio_usb bakes in its own CDC-only ones, so it
 * can't coexist with usb_descriptors.c. This drives tud_task() and the
 * CDC endpoints directly and registers a stdio_driver_t (same mechanism
 * lcd_console.c uses to mirror stdout to the LCD) so every existing
 * printf()/getchar() call site in main.c/editor.c/script.c needs zero
 * changes -- pico_stdio's out_chars/in_chars fan-out already supports
 * multiple simultaneously-registered drivers. */

static void cdc_stdio_out_chars(const char *buf, int len) {
    int i = 0;
    while (i < len) {
        int n = tud_cdc_write(buf + i, (uint32_t)(len - i));
        i += n;
        tud_cdc_write_flush();
        tud_task();
    }
}

static void cdc_stdio_out_flush(void) {
    tud_cdc_write_flush();
    tud_task();
}

static int cdc_stdio_in_chars(char *buf, int len) {
    tud_task();
    if (!tud_cdc_connected() || !tud_cdc_available()) return PICO_ERROR_NO_DATA;
    return (int)tud_cdc_read(buf, (uint32_t)len);
}

static stdio_driver_t cdc_stdio_driver = {
    .out_chars = cdc_stdio_out_chars,
    .out_flush = cdc_stdio_out_flush,
    .in_chars = cdc_stdio_in_chars,
    .set_chars_available_callback = NULL,
    .next = NULL,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .last_ended_with_cr = false,
    .crlf_enabled = PICO_STDIO_DEFAULT_CRLF,
#endif
};

void usb_init(void) {
    tud_init(BOARD_TUD_RHPORT);
    stdio_set_driver_enabled(&cdc_stdio_driver, true);
}

bool usb_console_connected(void) {
    tud_task();
    return tud_cdc_connected();
}

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
    } while (c == PICO_ERROR_TIMEOUT || c == PICO_ERROR_NO_DATA);
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
