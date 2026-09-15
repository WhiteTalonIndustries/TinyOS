#include "lcd_console.h"
#include "LCD_Driver.h"
#include "LCD_GUI.h"
#include "fonts.h"
#include "pico/stdio/driver.h"

/* Simple fixed-grid scrolling terminal on the LCD, using Font16 (see
 * fonts.h). This board's panel is 240x320 in the default orientation
 * (LCD_2_8_WIDTH/HEIGHT). No real scrolling (yet) -- when the grid fills,
 * the screen clears and starts again at the top-left; good enough as a
 * first cut for "the display should show the shell". */

#define TERM_BG RGB565_BLACK
#define TERM_FG RGB565_WHITE
#define RGB565_BLACK 0x0000
#define RGB565_WHITE 0xffff

static const sFONT *font = &Font16;
static int cols, rows;
static int cur_col, cur_row;

/* This codebase's only ANSI sequences are \033[2J (clear screen) and
 * \033[H (cursor home), both used by "clear" and the startup banner. Swallow
 * ESC ... <final byte 0x40-0x7e> entirely rather than rendering the raw
 * escape bytes as literal garbage text -- this is a plain mirror, not a
 * real terminal emulator, so \033[2J/\033[H are just treated as "clear". */
static enum { ANSI_NONE, ANSI_ESC, ANSI_CSI } ansi_state = ANSI_NONE;

void lcd_console_init(void) {
    cols = LCD_2_8_WIDTH / Font16.Width;
    rows = LCD_2_8_HEIGHT / Font16.Height;
    cur_col = 0;
    cur_row = 0;
    LCD_Clear(TERM_BG);
}

static void advance_line(void) {
    cur_col = 0;
    cur_row++;
    if (cur_row >= rows) {
        cur_row = 0;
        LCD_Clear(TERM_BG);
    }
}

static void lcd_console_putc(char c) {
    if (ansi_state == ANSI_ESC) {
        ansi_state = (c == '[') ? ANSI_CSI : ANSI_NONE;
        return;
    }
    if (ansi_state == ANSI_CSI) {
        if (c == 'J' || c == 'H') { /* clear-screen / cursor-home: reset our grid too */
            cur_col = 0;
            cur_row = 0;
            LCD_Clear(TERM_BG);
        }
        if (c >= 0x40 && c <= 0x7e) ansi_state = ANSI_NONE; /* final byte ends the sequence */
        return;
    }
    if (c == '\033') {
        ansi_state = ANSI_ESC;
        return;
    }

    if (c == '\r') {
        return; /* \n (which always follows \r in this codebase's console_puts) does the actual line break */
    } else if (c == '\n') {
        advance_line();
    } else if (c == '\b' || c == 127) {
        if (cur_col > 0) {
            cur_col--;
            GUI_DisChar((POINT)(cur_col * font->Width), (POINT)(cur_row * font->Height), ' ', (sFONT *)font, TERM_BG, TERM_FG);
        }
    } else if (c >= 0x20 && c < 0x7f) {
        if (cur_col >= cols) advance_line();
        GUI_DisChar((POINT)(cur_col * font->Width), (POINT)(cur_row * font->Height), c, (sFONT *)font, TERM_BG, TERM_FG);
        cur_col++;
    }
    /* anything else (other control/escape bytes) is silently ignored --
     * this is a plain text mirror, not a real ANSI terminal emulator. */
}

static void lcd_stdio_out_chars(const char *buf, int len) {
    int i;
    for (i = 0; i < len; i++) lcd_console_putc(buf[i]);
}

static stdio_driver_t lcd_stdio_driver = {
    .out_chars = lcd_stdio_out_chars,
    .out_flush = NULL,
    .in_chars = NULL,
    .set_chars_available_callback = NULL,
    .next = NULL,
};

void lcd_console_register_stdio(void) {
    stdio_set_driver_enabled(&lcd_stdio_driver, true);
}
