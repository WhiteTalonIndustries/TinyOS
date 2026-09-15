#include "status.h"
#include "LCD_Driver.h"
#include "LCD_GUI.h"
#include "fonts.h"

/* Splash-style status display: "TinyOS" rendered in the status color on a
 * black background, centered, rather than a full solid-color fill --
 * readable as a label, not just a color swatch. */

#define RGB565(r, g, b) (uint16_t)((((r) & 0xf8) << 8) | (((g) & 0xfc) << 3) | (((b) & 0xf8) >> 3))
#define BG RGB565(0, 0, 0)

static const char *label = "TinyOS";

void status_show(tinyos_status_t status) {
    uint16_t color;
    switch (status) {
        case STATUS_BOOTING:      color = RGB565(255, 255, 255); break;
        case STATUS_LCD_OK:       color = RGB565(0, 200, 0);     break;
        case STATUS_USB_WAITING:  color = RGB565(230, 200, 0);   break;
        case STATUS_USB_OK:       color = RGB565(0, 100, 255);   break;
        case STATUS_ERROR:        color = RGB565(220, 0, 0);     break;
        default:                  color = RGB565(255, 0, 255);  break; /* magenta: unknown code -- a bug in the caller */
    }

    LCD_Clear(BG);

    /* Screen is 320(w)x240(h) in the landscape orientation main.c uses
     * (LCD_2_8_HEIGHT x LCD_2_8_WIDTH -- see lcd_console.c's comment for
     * why those names are swapped here). Center the label. */
    {
        int text_width = (int)(Font24.Width * 6); /* strlen("TinyOS") == 6 */
        int x = (LCD_2_8_HEIGHT - text_width) / 2;
        int y = (LCD_2_8_WIDTH - Font24.Height) / 2;
        GUI_DisString_EN((POINT)x, (POINT)y, label, (sFONT *)&Font24, BG, color);
    }
}
