#include <stdint.h>
#include "tinyos_lcd.h"

/* Register-level ST7789 driver for the Waveshare Pico-ResTouch-LCD-2.8
 * (240x320) attached to this RP2350 board, built purely as a USB bring-up
 * debugging aid -- see lcd.h. Pin map and the ST7789 init register sequence
 * are taken directly from Waveshare's own reference driver at
 * resources/c/lib/config/DEV_Config.h and resources/c/lib/lcd/LCD_Driver.c
 * (their "LCD_2_8" branch, which is this exact panel -- confirmed against
 * their code, despite the file being named after the older ILI9486 3.5"
 * panel; the 2.8" board is ST7789).
 *
 * GPIO pins (see rp2350_hardware_facts memory note): RST=15, DC=8, CS=9,
 * CLK=10, MOSI=11 -- all on SPI1. This driver is write-only (never reads
 * MISO/GPIO12 back), since it only ever pushes pixel data out.
 *
 * IO_BANK0/PADS_BANK0 are NOT reset here -- usb.c's led_init() already did
 * that as part of Stage 1 boot, and resetting those shared blocks again
 * would clobber the already-configured backlight pin (GPIO13). Each pin
 * used here is configured individually instead. */

#define REG32(addr) (*(volatile uint32_t *)(addr))

#define IO_BANK0_BASE   0x40028000u
#define PADS_BANK0_BASE 0x40038000u
#define SIO_BASE        0xd0000000u
#define SPI1_BASE       0x40088000u
#define RESETS_BASE     0x40020000u
#define CLOCKS_BASE     0x40010000u

#define RESETS_RESET      REG32(RESETS_BASE + 0x00)
#define RESETS_RESET_DONE REG32(RESETS_BASE + 0x08)
#define RESET_BIT_SPI1    (1u << 19)

#define CLK_PERI_CTRL REG32(CLOCKS_BASE + 0x48)

#define LCD_RST_PIN  15u
#define LCD_DC_PIN   8u
#define LCD_CS_PIN   9u
#define LCD_CLK_PIN  10u
#define LCD_MOSI_PIN 11u
#define TP_CS_PIN    16u /* touch controller -- shares this SPI1 bus, must be deselected */
#define SD_CS_PIN    22u /* microSD slot -- shares this SPI1 bus, must be deselected */

#define SPI_SSPCR0  REG32(SPI1_BASE + 0x00)
#define SPI_SSPCR1  REG32(SPI1_BASE + 0x04)
#define SPI_SSPDR   REG32(SPI1_BASE + 0x08)
#define SPI_SSPSR   REG32(SPI1_BASE + 0x0c)
#define SPI_SSPCPSR REG32(SPI1_BASE + 0x10)

#define SPI_SSPSR_BSY (1u << 4)
#define SPI_SSPSR_TNF (1u << 1)

static void reset_block(uint32_t bit) {
    RESETS_RESET |= bit;
    RESETS_RESET &= ~bit;
    while (!(RESETS_RESET_DONE & bit));
}

static void pad_setup(uint32_t pin, uint32_t funcsel) {
    /* Confirmed by actually dumping the PAD register on this board after a
     * known-working LCD_Init() (via a diagnostic pico-sdk build printing
     * over USB serial -- see resources/c/regdump.c): the real, working
     * value is 0x56 for every LCD/touch/SD pin, i.e. SCHMITT=1, PDE=1,
     * DRIVE=01(4mA) IN ADDITION TO IE=1/OD=0/ISO=0. pico-sdk's
     * gpio_set_function() only ever does a MASKED write touching IE/OD (see
     * its file header comment: "This doesn't affect e.g. pullup/pulldown,
     * as these are in pad controls") plus a separate masked ISO-clear --
     * it never forces SCHMITT/PDE/PUE/DRIVE to 0 the way a full-register
     * overwrite does. Our first fix (`= (1u<<6)`) still did a full
     * overwrite, just with a different constant -- it stomped the same
     * fields, only IE was corrected. Read-modify-write here instead, to
     * leave whatever this chip's true silicon reset default is for every
     * field we don't specifically care about, exactly like pico-sdk does. */
    volatile uint32_t *pad = &REG32(PADS_BANK0_BASE + 0x04u + pin * 4u);
    *pad = (*pad & ~((1u << 8) | (1u << 7))) | (1u << 6); /* ISO=0, OD=0, IE=1; leave SCHMITT/PDE/PUE/DRIVE untouched */
    REG32(IO_BANK0_BASE + 0x04u + pin * 8u) = funcsel;
}

static void gpio_out(uint32_t pin) {
    pad_setup(pin, 5u); /* SIO */
    REG32(SIO_BASE + 0x038) = (1u << pin); /* GPIO_OE_SET */
}

static void gpio_write(uint32_t pin, int level) {
    REG32(SIO_BASE + (level ? 0x018u : 0x020u)) = (1u << pin); /* OUT_SET / OUT_CLR */
}

static void delay_ms(uint32_t ms) {
    /* Uncalibrated busy-wait -- clk_sys is ~125MHz by the time lcd_init()
     * runs (after clocks_init()), so this is deliberately generous rather
     * than exact; ST7789 reset/init timing just needs "at least" this long. */
    volatile uint32_t n = ms * 30000u;
    while (n--);
}

/* Bit-banged SPI over plain GPIO (SIO), mode 0 (CPOL=0/CPHA=0: idle clock
 * low, data sampled on the rising edge) -- MSB first, matching the ST7789's
 * expected protocol. Used instead of the hardware SPI1 peripheral as an
 * isolation test: SIO GPIO toggling is the one primitive already proven
 * reliable by every backlight blink checkpoint in this entire debugging
 * session, whereas the hardware-SPI1 path (clk_peri/CR0/CR1/CPSR/funcsel)
 * is new, unproven code that produced no visible panel response even after
 * fixing a real IE-bit bug found by comparing against pico-sdk. If this
 * bit-banged version works, the remaining bug is specifically in the SPI1
 * hardware bring-up; if it still doesn't, the bug is elsewhere (reset
 * timing, command sequence, or a wrong assumption about the panel/wiring). */
static void spi_write_byte(uint8_t b) {
    int i;
    for (i = 7; i >= 0; i--) {
        gpio_write(LCD_CLK_PIN, 0);
        gpio_write(LCD_MOSI_PIN, (b >> i) & 1u);
        gpio_write(LCD_CLK_PIN, 1); /* data latched on this rising edge */
    }
    gpio_write(LCD_CLK_PIN, 0);
}

static void lcd_cmd(uint8_t reg) {
    gpio_write(LCD_DC_PIN, 0);
    gpio_write(LCD_CS_PIN, 0);
    spi_write_byte(reg);
    gpio_write(LCD_CS_PIN, 1);
}

static void lcd_data8(uint8_t d) {
    gpio_write(LCD_DC_PIN, 1);
    gpio_write(LCD_CS_PIN, 0);
    spi_write_byte(d);
    gpio_write(LCD_CS_PIN, 1);
}

static void lcd_data_run(uint16_t color, uint32_t count) {
    uint8_t hi = (uint8_t)(color >> 8), lo = (uint8_t)color;
    uint32_t i;
    gpio_write(LCD_DC_PIN, 1);
    gpio_write(LCD_CS_PIN, 0);
    for (i = 0; i < count; i++) {
        spi_write_byte(hi);
        spi_write_byte(lo);
    }
    gpio_write(LCD_CS_PIN, 1);
}

static void lcd_set_window(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye) {
    lcd_cmd(0x2a); /* CASET */
    lcd_data8((uint8_t)(xs >> 8)); lcd_data8((uint8_t)xs);
    lcd_data8((uint8_t)((xe - 1) >> 8)); lcd_data8((uint8_t)(xe - 1));
    lcd_cmd(0x2b); /* RASET */
    lcd_data8((uint8_t)(ys >> 8)); lcd_data8((uint8_t)ys);
    lcd_data8((uint8_t)((ye - 1) >> 8)); lcd_data8((uint8_t)(ye - 1));
    lcd_cmd(0x2c); /* RAMWR */
}

void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    lcd_set_window(x, y, (uint16_t)(x + w), (uint16_t)(y + h));
    lcd_data_run(color, (uint32_t)w * (uint32_t)h);
}

void lcd_marker(uint16_t x, uint16_t y, uint16_t size, uint16_t color) {
    lcd_fill_rect(x, y, size, size, color);
}

/* Classic 7-segment encoding, bit0=a(top) 1=b(upper-right) 2=c(lower-right)
 * 3=d(bottom) 4=e(lower-left) 5=f(upper-left) 6=g(middle). Index 0-15 = hex
 * digit. Segment layout:
 *   _a_
 *  f   b
 *   _g_
 *  e   c
 *   _d_
 */
static const uint8_t seg_table[16] = {
    0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07,
    0x7f, 0x6f, 0x77, 0x7c, 0x39, 0x5e, 0x79, 0x71
};

static void lcd_seg_digit(uint16_t x, uint16_t y, uint16_t size, uint8_t digit, uint16_t color, uint16_t bg) {
    uint8_t segs = seg_table[digit & 0xfu];
    uint16_t w = size * 3u, thick = size;
    /* Clear the digit's cell first so changing an OFF segment to OFF still
     * erases whatever was drawn there on a previous update. */
    lcd_fill_rect(x, y, w, size * 5u, bg);
    if (segs & 0x01u) lcd_fill_rect(x, y, w, thick, color);                              /* a: top */
    if (segs & 0x20u) lcd_fill_rect(x, y, thick, size * 2u, color);                       /* f: upper-left */
    if (segs & 0x02u) lcd_fill_rect((uint16_t)(x + w - thick), y, thick, size * 2u, color); /* b: upper-right */
    if (segs & 0x40u) lcd_fill_rect(x, (uint16_t)(y + size * 2u), w, thick, color);        /* g: middle */
    if (segs & 0x10u) lcd_fill_rect(x, (uint16_t)(y + size * 2u), thick, size * 2u, color); /* e: lower-left */
    if (segs & 0x04u) lcd_fill_rect((uint16_t)(x + w - thick), (uint16_t)(y + size * 2u), thick, size * 2u, color); /* c: lower-right */
    if (segs & 0x08u) lcd_fill_rect(x, (uint16_t)(y + size * 4u), w, thick, color);        /* d: bottom */
}

void lcd_hex32(uint16_t x, uint16_t y, uint16_t size, uint32_t value, uint16_t color, uint16_t bg) {
    int i;
    uint16_t cell_w = (uint16_t)(size * 3u + size); /* digit width + gap */
    for (i = 7; i >= 0; i--) {
        uint8_t nibble = (uint8_t)((value >> (i * 4)) & 0xfu);
        lcd_seg_digit((uint16_t)(x + (7 - i) * cell_w), y, size, nibble, color, bg);
    }
}

static void lcd_reset(void) {
    gpio_write(LCD_RST_PIN, 1); delay_ms(50);
    gpio_write(LCD_RST_PIN, 0); delay_ms(50);
    gpio_write(LCD_RST_PIN, 1); delay_ms(50);
}

/* ST7789 init sequence, taken verbatim (values only, not the RP2040 SDK
 * calls) from Waveshare's LCD_InitReg()'s LCD_2_8 branch. */
static void lcd_init_reg(void) {
    lcd_cmd(0x11); delay_ms(100); /* sleep out */

    lcd_cmd(0x36); lcd_data8(0x00); /* MADCTL: default orientation */
    lcd_cmd(0x3a); lcd_data8(0x55); /* COLMOD: 16bpp */

    lcd_cmd(0xb2);
    lcd_data8(0x0c); lcd_data8(0x0c); lcd_data8(0x00); lcd_data8(0x33); lcd_data8(0x33);
    lcd_cmd(0xb7); lcd_data8(0x35);
    lcd_cmd(0xbb); lcd_data8(0x28);
    lcd_cmd(0xc0); lcd_data8(0x3c);
    lcd_cmd(0xc2); lcd_data8(0x01);
    lcd_cmd(0xc3); lcd_data8(0x0b);
    lcd_cmd(0xc4); lcd_data8(0x20);
    lcd_cmd(0xc6); lcd_data8(0x0f);
    lcd_cmd(0xd0); lcd_data8(0xa4); lcd_data8(0xa1);

    lcd_cmd(0xe0);
    lcd_data8(0xd0); lcd_data8(0x01); lcd_data8(0x08); lcd_data8(0x0f); lcd_data8(0x11);
    lcd_data8(0x2a); lcd_data8(0x36); lcd_data8(0x55); lcd_data8(0x44); lcd_data8(0x3a);
    lcd_data8(0x0b); lcd_data8(0x06); lcd_data8(0x11); lcd_data8(0x20);
    lcd_cmd(0xe1);
    lcd_data8(0xd0); lcd_data8(0x02); lcd_data8(0x07); lcd_data8(0x0a); lcd_data8(0x0b);
    lcd_data8(0x18); lcd_data8(0x34); lcd_data8(0x43); lcd_data8(0x4a); lcd_data8(0x2b);
    lcd_data8(0x1b); lcd_data8(0x1c); lcd_data8(0x22); lcd_data8(0x1f);

    lcd_cmd(0x55); lcd_data8(0xb0);
    lcd_cmd(0x29); /* display on */
}

void lcd_init(void) {
    gpio_out(LCD_RST_PIN);
    gpio_out(LCD_DC_PIN);
    gpio_out(LCD_CS_PIN);
    gpio_write(LCD_CS_PIN, 1);

    /* Touch controller and microSD slot share this SPI1 bus (same SCK/
     * MOSI/MISO lines, separate chip-selects) -- Waveshare's own reference
     * code explicitly drives every chip-select on the bus high (deselected)
     * before touching the LCD (see DEV_GPIO_Init() in DEV_Config.c). We'd
     * skipped this entirely: if TP_CS/SD_CS float low (undriven, no
     * pull -- plausible given the RP2350 ISO-bit default), that chip stays
     * selected and contends on the shared bus with every byte meant for the
     * LCD, which would explain total silence from the panel despite every
     * LCD-specific register/command being individually correct. */
    gpio_out(TP_CS_PIN);
    gpio_write(TP_CS_PIN, 1);
    gpio_out(SD_CS_PIN);
    gpio_write(SD_CS_PIN, 1);

    /* Bit-banged SPI isolation test (see spi_write_byte's comment): CLK/MOSI
     * as plain SIO outputs instead of routing to the SPI1 peripheral. */
    gpio_out(LCD_CLK_PIN);
    gpio_out(LCD_MOSI_PIN);
    gpio_write(LCD_CLK_PIN, 0);

    lcd_reset();
    lcd_init_reg();

    /* Bright, unmistakable color -- NOT black -- so a totally broken SPI
     * link (nothing ever reaches the panel) is visually distinct from a
     * working one, unlike the previous black fill which looked identical
     * either way. Swap back to LCD_BLACK once the SPI/reset path is
     * confirmed working. */
    lcd_fill_rect(0, 0, 240, 320, LCD_RED);
}
