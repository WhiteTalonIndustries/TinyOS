#ifndef TINYOS_TRNG_H
#define TINYOS_TRNG_H

/* RP2350's actual hardware TRNG (a Rambus/CryptoCell-family entropy IP
 * block at TRNG_BASE) -- a real random-number generator, not the
 * ADC-noise-based approximation in adc.c. Brings itself up lazily on
 * first use (pico-sdk's own runtime_init_early_resets() already takes
 * TRNG out of system reset before main() runs -- see hardware_regs/
 * resets.h's RESETS_RESET_TRNG bit and pico_runtime_init/runtime_init.c
 * -- so no RESETS-register handling is needed here at all). */

/* A random digit 0-9, drawn from the TRNG's entropy holding register (6 x
 * 32-bit words per hardware collection), rejection-sampled for a uniform
 * distribution and re-collected whenever a batch fails the TRNG's own
 * built-in health tests (Von Neumann, CRNGT, autocorrelation). Blocks
 * until a valid digit is available. */
int tinyos_trng_random_digit(void);

#endif
