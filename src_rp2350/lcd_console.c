#include "lcd_console.h"
#include "LCD_Driver.h"
#include "LCD_GUI.h"
#include "fonts.h"
#include "pico/stdio/driver.h"

/* Simple fixed-grid scrolling terminal on the LCD, using Font12 (see
 * fonts.h) -- a middle ground between Font8 (denser but harder to read)
 * and Font16 (very readable but few chars per line). main.c
 * inits the panel rotated 90deg clockwise (U2D_R2L), which swaps the
 * usable area to 320(w)x240(h) -- LCD_2_8_HEIGHT x LCD_2_8_WIDTH, not the
 * other way around; LCD_Driver.c's LCD_SetGramScanWay() does this same
 * swap internally for LCD_Clear()/etc, so this must match whatever
 * orientation main.c actually passed to LCD_Init().
 *
 * Real scrolling: a full grid worth of characters is kept in `text_buf`
 * so that when the last row fills, every row can be shifted up by one
 * and the whole screen repainted from the buffer -- unlike a per-cell
 * GUI_DisChar, this does mean a full-screen SPI redraw, but it only
 * happens once per scroll (every `rows` lines), not per keystroke. */

#define TERM_BG RGB565_BLACK
#define TERM_FG RGB565_WHITE
#define RGB565_BLACK 0x0000
#define RGB565_WHITE 0xffff

/* Generous upper bound on grid size for any font this console might use
 * (Font12 -- 7x12 -- currently gives 45x20); sized once, not computed
 * from the font, so it doesn't need to track a font change. */
#define TERM_MAX_COLS 64
#define TERM_MAX_ROWS 32

static const sFONT *font = &Font12;
static int cols, rows;
static int cur_col, cur_row;
static char text_buf[TERM_MAX_ROWS][TERM_MAX_COLS];

/* This codebase's only ANSI sequences are \033[2J (clear screen) and
 * \033[H (cursor home), both used by "clear" and the startup banner. Swallow
 * ESC ... <final byte 0x40-0x7e> entirely rather than rendering the raw
 * escape bytes as literal garbage text -- this is a plain mirror, not a
 * real terminal emulator, so \033[2J/\033[H are just treated as "clear". */
static enum { ANSI_NONE, ANSI_ESC, ANSI_CSI } ansi_state = ANSI_NONE;

static void clear_row(int row) {
    int c;
    for (c = 0; c < cols; c++) text_buf[row][c] = ' ';
}

static void redraw_screen(void) {
    int r, c;
    for (r = 0; r < rows; r++) {
        for (c = 0; c < cols; c++) {
            char ch = text_buf[r][c];
            GUI_DisChar((POINT)(c * font->Width), (POINT)(r * font->Height), ch ? ch : ' ', (sFONT *)font, TERM_BG, TERM_FG);
        }
    }
}

void lcd_console_init(void) {
    int r;
    cols = LCD_2_8_HEIGHT / Font12.Width;  /* 320px wide in the rotated (landscape) orientation */
    rows = LCD_2_8_WIDTH / Font12.Height;  /* 240px tall in the rotated (landscape) orientation */
    if (cols > TERM_MAX_COLS) cols = TERM_MAX_COLS;
    if (rows > TERM_MAX_ROWS) rows = TERM_MAX_ROWS;
    cur_col = 0;
    cur_row = 0;
    for (r = 0; r < rows; r++) clear_row(r);
    LCD_Clear(TERM_BG);
}

static void advance_line(void) {
    cur_col = 0;
    cur_row++;
    if (cur_row >= rows) {
        /* Scroll: drop the top row, shift everything else up one, clear
         * the newly-exposed bottom row, and repaint the whole grid from
         * the buffer -- a full 320x240 SPI redraw, but only once per
         * scroll rather than once per wrapped row. */
        int r;
        for (r = 1; r < rows; r++) {
            int c;
            for (c = 0; c < cols; c++) text_buf[r - 1][c] = text_buf[r][c];
        }
        clear_row(rows - 1);
        cur_row = rows - 1;
        redraw_screen();
    }
}

static void lcd_console_putc(char c) {
    if (ansi_state == ANSI_ESC) {
        ansi_state = (c == '[') ? ANSI_CSI : ANSI_NONE;
        return;
    }
    if (ansi_state == ANSI_CSI) {
        if (c == 'J' || c == 'H') { /* clear-screen / cursor-home: reset our grid too */
            int r;
            cur_col = 0;
            cur_row = 0;
            for (r = 0; r < rows; r++) clear_row(r);
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
            text_buf[cur_row][cur_col] = ' ';
            GUI_DisChar((POINT)(cur_col * font->Width), (POINT)(cur_row * font->Height), ' ', (sFONT *)font, TERM_BG, TERM_FG);
        }
    } else if (c >= 0x20 && c < 0x7f) {
        if (cur_col >= cols) advance_line();
        text_buf[cur_row][cur_col] = c;
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
