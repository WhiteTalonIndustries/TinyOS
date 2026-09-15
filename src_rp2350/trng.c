#include <stdint.h>
#include "trng.h"
#include "hardware/structs/trng.h"

/* Ground-truthed against pico-sdk's own generated headers
 * (hardware/regs/trng.h, hardware/structs/trng.h -- both derived from the
 * real RP2350 datasheet register list), NOT hand-derived from a generic
 * Rambus/CryptoCell TRNG datasheet -- an earlier draft of this file did
 * that and got the base address wrong (0x400e8000 vs the real
 * 0x400f0000, confirmed via hardware/regs/addressmap.h's TRNG_BASE) and
 * misread the RNG_ISR error-bit layout (real bits, confirmed via
 * hardware/regs/trng.h: bit0 EHR_VALID, bit1 AUTOCORR_ERR, bit2
 * CRNGT_ERR, bit3 VN_ERR -- not the "DATRDY/RCHT/APHT" bit names/positions
 * that draft assumed). pico-sdk's `trng_hw` struct pointer already
 * accounts for the register block starting at RNG_IMR (offset 0x100 from
 * TRNG_BASE), so this only ever touches it, never a raw address. */

#define ENTROPY_POOL_SIZE 24 /* 6 x 32-bit EHR words = 24 bytes per collection */

static uint8_t entropy_pool[ENTROPY_POOL_SIZE];
static int pool_fill;
static int inited;

static void trng_init_once(void) {
    if (inited) return;

    /* TRNG_SW_RESET is an internal reset local to the TRNG block itself
     * (distinct from the system RESETS peripheral, which pico-sdk's own
     * boot sequence already deasserted for TRNG before main() ran) --
     * just clears the block's internal state for a known-good start. */
    trng_hw->trng_sw_reset = 1;
    trng_hw->trng_sw_reset = 0;

    trng_hw->rng_icr = TRNG_RNG_ICR_BITS;       /* clear any stale status */
    trng_hw->rnd_source_enable = 1;             /* enable the entropy source */

    inited = 1;
}

/* Collects one fresh 192-bit batch, checks it against the TRNG's own
 * health tests, and refills entropy_pool[]. Returns 0 on success, -1 if
 * the batch failed a health test or the hardware didn't respond (caller
 * should just try again). */
static int trng_collect_batch(void) {
    uint32_t timeout;
    uint32_t isr;
    int w;

    trng_init_once();

    for (timeout = 1000000u; !(trng_hw->trng_valid & TRNG_TRNG_VALID_EHR_VALID_BITS); timeout--) {
        if (timeout == 0) return -1; /* TRNG not responding */
    }

    isr = trng_hw->rng_isr;
    if (isr & (TRNG_RNG_ISR_VN_ERR_BITS | TRNG_RNG_ISR_CRNGT_ERR_BITS | TRNG_RNG_ISR_AUTOCORR_ERR_BITS)) {
        trng_hw->rng_icr = TRNG_RNG_ICR_BITS; /* discard this batch, don't trust it */
        return -1;
    }

    for (w = 0; w < 6; w++) {
        uint32_t word = trng_hw->ehr_data[w];
        entropy_pool[w * 4 + 0] = (uint8_t)(word >> 0);
        entropy_pool[w * 4 + 1] = (uint8_t)(word >> 8);
        entropy_pool[w * 4 + 2] = (uint8_t)(word >> 16);
        entropy_pool[w * 4 + 3] = (uint8_t)(word >> 24);
    }
    trng_hw->rng_icr = TRNG_RNG_ICR_BITS; /* clear EHR_VALID, ready for next collection */
    pool_fill = ENTROPY_POOL_SIZE;
    return 0;
}

int tinyos_trng_random_digit(void) {
    while (1) {
        uint8_t b;

        if (pool_fill == 0) {
            int attempts;
            for (attempts = 0; trng_collect_batch() != 0; attempts++) {
                if (attempts > 100) return -1; /* hardware fault -- give up rather than hang forever */
            }
        }

        b = entropy_pool[ENTROPY_POOL_SIZE - pool_fill];
        pool_fill--;

        /* Rejection sampling: 250 = 25*10, so keeping bytes < 250 and
         * taking b % 10 gives a uniform 0-9 with no modulo bias (bytes
         * 250-255 would otherwise slightly favor digits 0-5). */
        if (b < 250) return b % 10;
    }
}
