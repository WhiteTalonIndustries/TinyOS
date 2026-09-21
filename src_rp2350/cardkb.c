#include "cardkb.h"
#include "hardware/i2c.h"
#include "pico/stdio/driver.h"
#include "pico/stdlib.h"

#define CARDKB_I2C_ADDR   0x5F
#define CARDKB_POLL_MS    20   /* ~50Hz -- plenty for a human typing, low I2C overhead */
#define CARDKB_TIMEOUT_US 2000 /* bounded, so a poll with nothing plugged in can't stall the shared getchar() loop */

/* Fn+arrow codes (not plain ASCII) -- see cardkb.h for where these came
 * from. Translated to the same ESC [ A/B/C/D sequences a real VT100
 * terminal sends, since that's what editor.c's arrow-key handling
 * already expects from console_getc() -- no changes needed there. */
#define CARDKB_KEY_LEFT  0xB4
#define CARDKB_KEY_UP    0xB5
#define CARDKB_KEY_DOWN  0xB6
#define CARDKB_KEY_RIGHT 0xB7

static uint8_t queue[3];
static int queue_len;
static int queue_pos;
static absolute_time_t next_poll_time;

static void queue_bytes(const uint8_t *bytes, int n) {
    int i;
    for (i = 0; i < n; i++) queue[i] = bytes[i];
    queue_len = n;
    queue_pos = 0;
}

static int cardkb_in_chars(char *buf, int len) {
    uint8_t key;
    int rc;
    (void)len; /* only ever asked for 1 byte at a time by pico-sdk's stdio_get_until */

    if (queue_pos < queue_len) {
        buf[0] = (char)queue[queue_pos++];
        return 1;
    }

    if (!time_reached(next_poll_time)) return PICO_ERROR_NO_DATA;
    next_poll_time = make_timeout_time_ms(CARDKB_POLL_MS);

    rc = i2c_read_timeout_us(i2c0, CARDKB_I2C_ADDR, &key, 1, false, CARDKB_TIMEOUT_US);
    if (rc != 1 || key == 0) return PICO_ERROR_NO_DATA;

    /* Sanity-check the byte before trusting it: a real CardKB only ever
     * sends printable ASCII, Enter/Backspace/Tab/Esc, or the four
     * Fn+arrow codes below. Anything else is bus noise (a marginal
     * STEMMA cable, or another intermittent electrical issue) rather
     * than a real keystroke -- treat it as no data instead of echoing
     * junk to the console. */
    if (key != 0x08 && key != 0x09 && key != 0x0D && key != 0x1B &&
        !(key >= 0x20 && key <= 0x7E) &&
        !(key >= CARDKB_KEY_LEFT && key <= CARDKB_KEY_RIGHT)) {
        return PICO_ERROR_NO_DATA;
    }

    switch (key) {
        case CARDKB_KEY_LEFT:  { uint8_t seq[3] = {0x1b, '[', 'D'}; queue_bytes(seq, 3); break; }
        case CARDKB_KEY_UP:    { uint8_t seq[3] = {0x1b, '[', 'A'}; queue_bytes(seq, 3); break; }
        case CARDKB_KEY_DOWN:  { uint8_t seq[3] = {0x1b, '[', 'B'}; queue_bytes(seq, 3); break; }
        case CARDKB_KEY_RIGHT: { uint8_t seq[3] = {0x1b, '[', 'C'}; queue_bytes(seq, 3); break; }
        default:               { uint8_t seq[1] = {key};           queue_bytes(seq, 1); break; }
    }

    buf[0] = (char)queue[queue_pos++];
    return 1;
}

static stdio_driver_t cardkb_stdio_driver = {
    .out_chars = NULL,
    .out_flush = NULL,
    .in_chars = cardkb_in_chars,
    .set_chars_available_callback = NULL,
    .next = NULL,
};

void cardkb_init(void) {
    i2c_init(i2c0, 100 * 1000);
    gpio_set_function(PICO_DEFAULT_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(PICO_DEFAULT_I2C_SDA_PIN);
    gpio_pull_up(PICO_DEFAULT_I2C_SCL_PIN);

    next_poll_time = get_absolute_time();
    stdio_set_driver_enabled(&cardkb_stdio_driver, true);
}
