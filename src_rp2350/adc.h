#ifndef TINYOS_ADC_H
#define TINYOS_ADC_H

/* Brings up the RP2350 ADC (pico-sdk hardware_adc). Named tinyos_adc_init,
 * not adc_init, because pico-sdk's own hardware/adc.h already declares a
 * function called adc_init() -- same name, different purpose, and both are
 * externally-linked C functions, so they can't coexist under one name in
 * the same binary. */
void tinyos_adc_init(void);

/* A random digit 0-9, drawn from GPIO40's floating-pin noise XORed with the
 * internal temperature sensor's LSB jitter, Von Neumann debiased. A cheap,
 * genuinely non-deterministic entropy source -- not cryptographically
 * secure, but fine for scripts wanting casual randomness (dice, shuffling,
 * etc). Blocks until a valid digit is drawn (rejection sampling).
 *
 * GPIO40 (not GPIO26) because this board is the QFN80 package (RP2350B,
 * confirmed via `picotool info -d`), which moves the ADC-capable pins to
 * GPIO40-47 -- see rp2350_hardware_facts memory note. */
int adc_random_digit(void);

#endif
