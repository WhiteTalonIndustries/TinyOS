#include <stdint.h>
#include "adc.h"
#include "hardware/adc.h"
#include "hardware/timer.h"

/* pico-sdk-backed ADC. ADC_BASE_PIN/NUM_ADC_CHANNELS resolve correctly to
 * GPIO40-47 (channels 0-7) + temp sensor (channel 8) automatically, as long
 * as the build targets the correct board -- see README_RP2350.md: this
 * board is `pimoroni_pico_plus2_w_rp2350` (RP2350B/QFN80), NOT `pico2_w`
 * (RP2350A) -- the two differ in exactly this (PICO_RP2350A 0 vs 1), among
 * other things (flash/PSRAM size, CYW43 pins). Building against the wrong
 * board here would silently read GPIO26 (not ADC-capable on this chip)
 * instead of GPIO40. */

void tinyos_adc_init(void) {
    adc_init(); /* pico-sdk's hardware_adc adc_init(), not this file's own function */
    adc_gpio_init(ADC_BASE_PIN); /* GPIO40 on this board */
    adc_set_temp_sensor_enabled(true);
}

static uint16_t adc_read_channel(uint32_t chan) {
    adc_select_input(chan);
    return adc_read();
}

/* A floating GPIO40 sampled back-to-back in a tight loop can read almost
 * identically from one conversion to the next -- there's no time for its
 * parasitic capacitance to pick up fresh thermal/RF noise between samples
 * only a few us apart. Mixing in a timer bit guarantees this changes call
 * to call regardless of how quiet the analog noise is at any given moment. */
static int entropy_bit(void) {
    int floating_pin = (int)(adc_read_channel(0) & 1u);
    int timer_jitter  = (int)(time_us_32() & 1u);
    int temp_sample   = (int)(adc_read_channel(ADC_TEMPERATURE_CHANNEL_NUM) & 1u);
    return floating_pin ^ temp_sample ^ timer_jitter;
}

/* Von Neumann debiasing: sample pairs, keep only 01/10 and discard 00/11.
 * Cancels any first-order bias in either noise source. Bounded: if the
 * combined source is somehow still stuck after many attempts, fall back to
 * the timer alone rather than hang -- degraded quality beats an unresponsive
 * shell. */
static int debiased_bit(void) {
    int attempt;
    for (attempt = 0; attempt < 1000; attempt++) {
        int b1 = entropy_bit();
        int b2 = entropy_bit();
        if (b1 != b2) return b1;
    }
    return (int)(time_us_32() & 1u);
}

int adc_random_digit(void) {
    int attempt;
    for (attempt = 0; attempt < 100; attempt++) {
        int i, val = 0;
        for (i = 0; i < 4; i++) val = (val << 1) | debiased_bit();
        if (val <= 9) return val; /* rejection sampling: uniform 0-9 from a 4-bit draw */
    }
    return (int)(time_us_32() % 10u); /* bounded fallback, astronomically unlikely to be reached */
}
