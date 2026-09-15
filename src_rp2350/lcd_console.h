#ifndef TINYOS_LCD_CONSOLE_H
#define TINYOS_LCD_CONSOLE_H

/* Mirrors the USB shell's output (and echoed input) onto the LCD as a
 * simple scrolling text terminal, using Waveshare's own GUI_DisChar()
 * (lib/lcd/LCD_GUI.c + lib/font/). Call lcd_console_init() once after
 * LCD_Init(), then lcd_console_register_stdio() once after
 * stdio_init_all() -- registers a pico-sdk stdio_driver_t that mirrors
 * EVERY character written to stdout (printf, putchar, anywhere in the
 * codebase) onto the LCD, not just calls that happen to go through this
 * file directly. */
void lcd_console_init(void);
void lcd_console_register_stdio(void);

#endif
