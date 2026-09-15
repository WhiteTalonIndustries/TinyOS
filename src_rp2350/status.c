#include "status.h"
#include "LCD_Driver.h"

#define RGB565(r, g, b) (uint16_t)((((r) & 0xf8) << 8) | (((g) & 0xfc) << 3) | (((b) & 0xf8) >> 3))

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
    LCD_Clear(color);
}
