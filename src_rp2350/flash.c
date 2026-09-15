#include <stdint.h>
#include "flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

/* pico-sdk-backed flash access -- see README_RP2350.md for why this port
 * uses pico-sdk rather than a bare-metal boot-ROM function-table lookup
 * (which is what src_2040/flash.c does). Single-core, no multicore_launch,
 * so the simple save_and_disable_interrupts() pattern is sufficient to
 * keep flash inaccessible-during-erase/program safe -- no need for the
 * full flash_safe_execute() multicore-lockout machinery. */

void flash_init(void) {
    /* Nothing to do -- pico-sdk's XIP is already up by the time main() runs. */
}

void flash_erase(uint32_t offset, uint32_t count) {
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(offset, count);
    restore_interrupts(ints);
}

void flash_program(uint32_t offset, const uint8_t *data, uint32_t count) {
    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(offset, data, count);
    restore_interrupts(ints);
}

const uint8_t *flash_read_ptr(uint32_t offset) {
    return (const uint8_t *)(FLASH_XIP_BASE + offset);
}
