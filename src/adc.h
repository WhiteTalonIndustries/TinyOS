#ifndef TINYOS_ADC_H
#define TINYOS_ADC_H

/* Brings up the RP2040 ADC. Requires clk_adc to already be running (see
 * clocks_init() in usb.c) -- without it this hangs forever waiting for a
 * conversion that never completes. */
void adc_init(void);

/* A random digit 0-9, drawn from GPIO26's floating-pin noise XORed with the
 * internal temperature sensor's LSB jitter, Von Neumann debiased. A cheap,
 * genuinely non-deterministic entropy source -- not cryptographically
 * secure, but fine for scripts wanting casual randomness (dice, shuffling,
 * etc). Blocks until a valid digit is drawn (rejection sampling). */
int adc_random_digit(void);

#endif
