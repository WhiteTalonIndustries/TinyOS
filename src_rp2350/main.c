#include "LCD_Driver.h"
#include "DEV_Config.h"
#include "status.h"
#include "pico/stdlib.h"

/* Display status codes (see status.h), built on pico-sdk + Waveshare's own
 * proven DEV_Config.c/LCD_Driver.c. Cycles through every status color on a
 * timer for now, purely to confirm each one renders correctly on hardware;
 * once confirmed, real subsystems (USB bring-up, etc.) will call
 * status_show() directly instead of this demo loop. */

int main(void) {
    System_Init();
    LCD_Init(SCAN_DIR_DFT, 800);

    status_show(STATUS_BOOTING);
    sleep_ms(2000);
    status_show(STATUS_LCD_OK);
    sleep_ms(2000);
    status_show(STATUS_USB_WAITING);
    sleep_ms(2000);
    status_show(STATUS_USB_OK);
    sleep_ms(2000);
    status_show(STATUS_ERROR);

    while (1) {
        tight_loop_contents();
    }
}
