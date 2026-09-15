#ifndef TINYOS_LCD_H
#define TINYOS_LCD_H

#include <stdint.h>

/* Minimal ST7789 (Waveshare Pico-ResTouch-LCD-2.8, 240x320) debug-output
 * driver -- bit-banged GPIO for RST/DC/CS, hardware SPI1 for data. Built
 * purely as a USB bring-up debugging aid (much higher bandwidth than
 * counting backlight blinks) -- not a general graphics library. See
 * lcd.c's file header for the register-level bring-up details and the
 * ST7789 init sequence, taken from Waveshare's own reference driver at
 * resources/c/lib/lcd/LCD_Driver.c.
 *
 * Must be called after console_init() (which already brings up
 * IO_BANK0/PADS_BANK0 -- lcd_init() does NOT re-reset those shared blocks,
 * to avoid disturbing the already-configured backlight pin). */
void lcd_init(void);

void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/* Draws an 8-hex-digit dump of `value` as blocky 7-segment digits, `size`
 * pixels per segment-cell (try 4-6), top-left corner at (x,y). */
void lcd_hex32(uint16_t x, uint16_t y, uint16_t size, uint32_t value, uint16_t color, uint16_t bg);

/* Small solid square, used as a row label/status marker (e.g. green =
 * good, red = bad) since this driver has no text font. */
void lcd_marker(uint16_t x, uint16_t y, uint16_t size, uint16_t color);

/* RGB565 helper */
#define LCD_RGB(r, g, b) (uint16_t)((((r) & 0xf8u) << 8) | (((g) & 0xfcu) << 3) | (((b) & 0xf8u) >> 3))
#define LCD_BLACK   LCD_RGB(0, 0, 0)
#define LCD_WHITE   LCD_RGB(255, 255, 255)
#define LCD_RED     LCD_RGB(255, 0, 0)
#define LCD_GREEN   LCD_RGB(0, 255, 0)
#define LCD_YELLOW  LCD_RGB(255, 255, 0)
#define LCD_BLUE    LCD_RGB(0, 100, 255)

#endif
