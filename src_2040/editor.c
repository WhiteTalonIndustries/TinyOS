#include <stdint.h>
#include "editor.h"
#include "usb.h"
#include "fs.h"

#define EDIT_BUF_SIZE 4096

static char buf[EDIT_BUF_SIZE];
static uint32_t len;
static uint32_t cursor;

static void put_udec(uint32_t v) {
    char tmp[10];
    int n = 0;
    if (v == 0) { console_putc('0'); return; }
    while (v > 0 && n < 10) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    while (n > 0) console_putc(tmp[--n]);
}

static uint32_t line_start(uint32_t idx) {
    while (idx > 0 && buf[idx - 1] != '\n') idx--;
    return idx;
}

static uint32_t line_end(uint32_t idx) {
    while (idx < len && buf[idx] != '\n') idx++;
    return idx;
}

static void redraw(const char *filename, int dirty) {
    uint32_t row = 0, col = 0, i;
    console_puts("\033[2J\033[H");
    console_puts("-- TinyOS nano-lite: ");
    console_puts(filename);
    if (dirty) console_puts(" [modified]");
    console_puts(" --  ^O save   ^X exit\r\n\r\n");
    console_write(buf, len);
    for (i = 0; i < cursor; i++) {
        if (buf[i] == '\n') { row++; col = 0; }
        else col++;
    }
    console_puts("\033[");
    put_udec(row + 3); /* 2 header lines + 1 blank line above the text */
    console_puts(";");
    put_udec(col + 1);
    console_puts("H");
}

void nano_edit(const char *filename) {
    int dirty = 0;
    int running = 1;
    int had_cr = 0;
    int n = fs_read(filename, buf, EDIT_BUF_SIZE);

    len = (n < 0) ? 0 : (uint32_t)n;
    cursor = len;

    redraw(filename, dirty);

    while (running) {
        char c = console_getc();

        if (had_cr && c == '\n') {
            had_cr = 0;
            continue;
        }
        had_cr = 0;

        if (c == 0x1b) { /* ESC: possible arrow key sequence */
            char c1 = console_getc();
            if (c1 == '[') {
                char c2 = console_getc();
                if (c2 == 'D') {
                    if (cursor > 0) cursor--;
                } else if (c2 == 'C') {
                    if (cursor < len) cursor++;
                } else if (c2 == 'A') {
                    uint32_t ls = line_start(cursor);
                    uint32_t col = cursor - ls;
                    if (ls > 0) {
                        uint32_t prev_end = ls - 1;
                        uint32_t prev_start = line_start(prev_end);
                        uint32_t prev_len = prev_end - prev_start;
                        cursor = prev_start + (col < prev_len ? col : prev_len);
                    }
                } else if (c2 == 'B') {
                    uint32_t le = line_end(cursor);
                    if (le < len) {
                        uint32_t ls = line_start(cursor);
                        uint32_t col = cursor - ls;
                        uint32_t next_start = le + 1;
                        uint32_t next_end = line_end(next_start);
                        uint32_t next_len = next_end - next_start;
                        cursor = next_start + (col < next_len ? col : next_len);
                    }
                }
            }
        } else if (c == 0x0f) { /* Ctrl+O: write-out (matches real nano; avoids
                                  * Ctrl+S, which terminals treat as XOFF and
                                  * eat before it ever reaches the device) */
            buf[len] = '\0';
            if (fs_write(filename, buf) == 0) dirty = 0;
        } else if (c == 0x18) { /* Ctrl+X */
            running = 0;
            continue;
        } else if (c == '\r') {
            if (len < EDIT_BUF_SIZE - 1) {
                uint32_t i;
                for (i = len; i > cursor; i--) buf[i] = buf[i - 1];
                buf[cursor] = '\n';
                len++;
                cursor++;
                dirty = 1;
            }
            had_cr = 1;
        } else if (c == 0x7f || c == 0x08) { /* backspace */
            if (cursor > 0) {
                uint32_t i;
                for (i = cursor - 1; i < len - 1; i++) buf[i] = buf[i + 1];
                len--;
                cursor--;
                dirty = 1;
            }
        } else if (c >= 0x20 && c < 0x7f) { /* printable */
            if (len < EDIT_BUF_SIZE - 1) {
                uint32_t i;
                for (i = len; i > cursor; i--) buf[i] = buf[i - 1];
                buf[cursor] = c;
                len++;
                cursor++;
                dirty = 1;
            }
        }

        redraw(filename, dirty);
    }

    console_puts("\033[2J\033[H");
}
