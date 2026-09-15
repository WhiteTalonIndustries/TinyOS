/* Absolute minimal test: does overriding runtime_init_clocks() with pure
 * GPIO blinking (zero clock code) complete and reach main() reliably? */
#include <stdint.h>

const char *tinyos_clock_checkpoints[1] = { "n/a" };
int tinyos_clock_checkpoint_count = 0;

void runtime_init_clocks(void) {
    volatile uint32_t *reset = (volatile uint32_t *)0x40020000u;
    volatile uint32_t *reset_done = (volatile uint32_t *)0x40020008u;
    volatile uint32_t n;
    int i;

    *reset |= (1u << 6) | (1u << 9);
    *reset &= ~((1u << 6) | (1u << 9));
    while (!(*reset_done & ((1u << 6) | (1u << 9))));
    *(volatile uint32_t *)(0x40038000u + 0x04u + 13u * 4u) = (1u << 6);
    *(volatile uint32_t *)(0x40028000u + 0x04u + 13u * 8u) = 5u;
    *(volatile uint32_t *)(0xd0000038u) = (1u << 13);

    for (i = 0; i < 5; i++) {
        *(volatile uint32_t *)(0xd0000018u) = (1u << 13);
        for (n = 2000000u; n; n--);
        *(volatile uint32_t *)(0xd0000020u) = (1u << 13);
        for (n = 2000000u; n; n--);
    }
    *(volatile uint32_t *)(0xd0000018u) = (1u << 13); /* end solid on */
}
