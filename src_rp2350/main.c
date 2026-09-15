#include "LCD_Driver.h"
#include "DEV_Config.h"
#include "pico/stdlib.h"

/* Minimal solid-color display test, built on pico-sdk + Waveshare's own
 * proven DEV_Config.c/LCD_Driver.c (see resources/c/examples/lcd_test.c for
 * the original reference this is modeled on) -- touch/GUI/Bmp/fatfs/sdcard
 * intentionally excluded, this is a debugging-output display, not yet a
 * full graphics stack. First goal: confirm the display lights up with a
 * solid color at all; next steps build error/status codes on top of this
 * (different colors, then simple shapes/digits) for debugging other
 * subsystems. */

#define RGB565(r, g, b) (uint16_t)((((r) & 0xf8) << 8) | (((g) & 0xfc) << 3) | (((b) & 0xf8) >> 3))
#define COLOR_RED RGB565(255, 0, 0)

int main(void) {
    System_Init();
    LCD_Init(SCAN_DIR_DFT, 800);
    LCD_Clear(COLOR_RED);

    while (1) {
        tight_loop_contents();
    }
}
