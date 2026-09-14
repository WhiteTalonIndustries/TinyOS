/* Diagnostic-only: bring up the LCD exactly like lcd_test() does (proven to
 * work), then dump the raw register values that resulted, over USB serial.
 * Used to compare against TinyOS's own from-scratch bare-metal LCD driver,
 * which produces a black screen despite an apparently identical command
 * sequence -- this gives a ground-truth register dump to diff against
 * without needing a logic analyzer. */
#include "LCD_Driver.h"
#include "DEV_Config.h"
#include "pico/stdlib.h"
#include <stdio.h>

#define REG32(addr) (*(volatile uint32_t *)(addr))

static void dump_pin(const char *name, uint32_t pin) {
    uint32_t ctrl = REG32(IO_BANK0_BASE + 0x04u + pin * 8u);
    uint32_t pad  = REG32(PADS_BANK0_BASE + 0x04u + pin * 4u);
    printf("GPIO%-2lu (%-4s): CTRL=0x%08lx  PAD=0x%08lx\n", (unsigned long)pin, name, (unsigned long)ctrl, (unsigned long)pad);
}

int main(void) {
    stdio_init_all();
    sleep_ms(3000); /* give the host time to open the serial port */

    System_Init();
    LCD_Init(SCAN_DIR_DFT, 800);

    while (1) {
        printf("\n=== register dump after known-working LCD_Init() ===\n");
        dump_pin("RST", LCD_RST_PIN);
        dump_pin("DC", LCD_DC_PIN);
        dump_pin("CS", LCD_CS_PIN);
        dump_pin("CLK", LCD_CLK_PIN);
        dump_pin("MOSI", LCD_MOSI_PIN);
        dump_pin("MISO", LCD_MISO_PIN);
        dump_pin("TP_CS", TP_CS_PIN);
        dump_pin("SD_CS", SD_CS_PIN);
        printf("SPI1: CR0=0x%08lx CR1=0x%08lx CPSR=0x%08lx DMACR=0x%08lx\n",
               (unsigned long)REG32(SPI1_BASE + 0x00), (unsigned long)REG32(SPI1_BASE + 0x04),
               (unsigned long)REG32(SPI1_BASE + 0x10), (unsigned long)REG32(SPI1_BASE + 0x24));
        printf("CLOCKS: CLK_PERI_CTRL=0x%08lx\n", (unsigned long)REG32(CLOCKS_BASE + 0x48));
        printf("RESETS: RESET=0x%08lx RESET_DONE=0x%08lx\n",
               (unsigned long)REG32(RESETS_BASE + 0x00), (unsigned long)REG32(RESETS_BASE + 0x08));
        sleep_ms(3000);
    }
    return 0;
}
