#include <stdint.h>
#include "adc.h"

#define REG32(addr) (*(volatile uint32_t *)(addr))

#define RESETS_BASE        0x4000c000u
#define RESETS_RESET       REG32(RESETS_BASE + 0x00)
#define RESETS_RESET_DONE  REG32(RESETS_BASE + 0x08)
#define RESET_BIT_ADC      (1u << 0)

#define PADS_BANK0_BASE 0x4001c000u
#define PADS_GPIO26     REG32(PADS_BANK0_BASE + 0x6c) /* GPIO26 = ADC channel 0 */
#define PADS_IE_BIT     (1u << 6) /* input buffer enable */
#define PADS_PUE_BIT    (1u << 3) /* pull-up enable */
#define PADS_PDE_BIT    (1u << 2) /* pull-down enable */

#define ADC_BASE   0x4004c000u
#define ADC_CS     REG32(ADC_BASE + 0x00)
#define ADC_RESULT REG32(ADC_BASE + 0x04)

#define ADC_CS_EN          (1u << 0)
#define ADC_CS_TS_EN       (1u << 1) /* internal temperature sensor enable */
#define ADC_CS_START_ONCE  (1u << 2)
#define ADC_CS_READY       (1u << 8)
#define ADC_CS_AINSEL_LSB  12u
#define ADC_CS_AINSEL_MASK (0x7u << ADC_CS_AINSEL_LSB)

#define ADC_CHANNEL_GPIO26     0u
#define ADC_CHANNEL_TEMP_SENSOR 4u

static void reset_block(uint32_t bit) {
    RESETS_RESET |= bit;
    RESETS_RESET &= ~bit;
    while (!(RESETS_RESET_DONE & bit));
}

void adc_init(void) {
    reset_block(RESET_BIT_ADC);

    /* Disable GPIO26's digital input buffer and pulls so it floats freely
     * as a noise source, matching pico-sdk's adc_gpio_init(). */
    PADS_GPIO26 &= ~(PADS_IE_BIT | PADS_PUE_BIT | PADS_PDE_BIT);

    ADC_CS = ADC_CS_EN | ADC_CS_TS_EN;
}

static uint32_t adc_read_channel(uint32_t chan) {
    ADC_CS = (ADC_CS & ~ADC_CS_AINSEL_MASK) | (chan << ADC_CS_AINSEL_LSB) | ADC_CS_EN | ADC_CS_TS_EN;
    ADC_CS |= ADC_CS_START_ONCE;
    while (!(ADC_CS & ADC_CS_READY));
    return ADC_RESULT & 0xfffu;
}

static int entropy_bit(void) {
    int floating_pin = (int)(adc_read_channel(ADC_CHANNEL_GPIO26) & 1u);
    int temp_sample   = (int)(adc_read_channel(ADC_CHANNEL_TEMP_SENSOR) & 1u);
    return floating_pin ^ temp_sample;
}

/* Von Neumann debiasing: sample pairs, keep only 01/10 and discard 00/11.
 * Cancels any first-order bias in either noise source. */
static int debiased_bit(void) {
    for (;;) {
        int b1 = entropy_bit();
        int b2 = entropy_bit();
        if (b1 != b2) return b1;
    }
}

int adc_random_digit(void) {
    for (;;) {
        int i, val = 0;
        for (i = 0; i < 4; i++) val = (val << 1) | debiased_bit();
        if (val <= 9) return val; /* rejection sampling: uniform 0-9 from a 4-bit draw */
    }
}
