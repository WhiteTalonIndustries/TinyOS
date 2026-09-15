/* Diagnostic-only: runs TinyOS's own bare-metal src_rp2350/lcd.c
 * (copied verbatim here as tinyos_lcd.c/.h) inside a normal, fully-working
 * pico-sdk environment. If the screen turns red here, the bug is somewhere
 * in TinyOS's own boot/clock environment, not lcd.c's logic. If it stays
 * black here too, the bug is definitively inside lcd.c itself -- and this
 * harness has printf available to find it directly. */
#include "tinyos_lcd.h"
#include "pico/stdlib.h"
#include <stdio.h>

extern const char *tinyos_clock_checkpoints[];
extern int tinyos_clock_checkpoint_count;

int main(void) {
    stdio_init_all();
    sleep_ms(3000);

    printf("=== tinyos_clocks_override checkpoints (ran before main(), via runtime_init) ===\n");
    for (int i = 0; i < tinyos_clock_checkpoint_count; i++) {
        printf("  %d: %s\n", i, tinyos_clock_checkpoints[i]);
    }

    /* lcd.c never touches the backlight -- that's TinyOS's usb.c's job.
     * Without this, the panel could be perfectly correct and totally
     * invisible. GPIO13, active high (matches usb.c's led_init()). */
    gpio_init(13);
    gpio_set_dir(13, GPIO_OUT);
    gpio_put(13, 1);

    printf("about to call tinyos lcd_init()...\n");
    lcd_init();
    printf("lcd_init() returned -- screen should be solid red now.\n");
    while (1) {
        sleep_ms(1000);
        printf("still alive\n");
    }
    return 0;
}
