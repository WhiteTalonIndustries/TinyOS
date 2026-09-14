#include <stdint.h>
#include "flash.h"

/* ================= RP2350 boot ROM function lookup =================
 * RP2350's boot ROM lookup mechanism is NOT the same as RP2040's, even
 * though the flash helper functions themselves (same two-character codes:
 * 'IF'/'EX'/'RE'/'RP'/'FC'/'CX') are unchanged -- confirmed against
 * pico-sdk's src/rp2_common/boot_bootrom_headers/include/boot/bootrom_constants.h
 * and src/rp2_common/pico_bootrom/include/pico/bootrom.h:
 *
 * RP2040: a 16-bit pointer at fixed ROM address 0x18 gives the lookup
 * function, which takes a (table, code) pair -- table found via another
 * fixed pointer at 0x14.
 *
 * RP2350 (Arm): the lookup function pointer lives at a DIFFERENT fixed ROM
 * address, 0x16 (BOOTROM_FUNC_TABLE_OFFSET(0x14) + BOOTROM_WELL_KNOWN_PTR_SIZE(2)),
 * and it takes (code, flags) directly -- no separate table pointer needed.
 * `flags` must be RT_FLAG_FUNC_ARM_SEC (0x4) for this image, which runs
 * entirely in Secure state (no TrustZone partitioning is set up -- see
 * crt0_rp2350.s). Getting either the offset or the flag wrong here doesn't
 * hang the chip the way the clk_sys glitchless-switch bug does -- it just
 * makes rom_func_lookup() return NULL, which would fault the first time any
 * flash_* function below is called through a NULL pointer. */

#define ROM_TABLE_CODE(c1, c2) ((uint32_t)(uint8_t)(c1) | ((uint32_t)(uint8_t)(c2) << 8))

#define BOOTROM_TABLE_LOOKUP_OFFSET 0x16u
#define RT_FLAG_FUNC_ARM_SEC        0x0004u

typedef void *(*rom_table_lookup_fn)(uint32_t code, uint32_t flags);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
static void *rom_func_lookup(uint32_t code) {
    rom_table_lookup_fn lookup = (rom_table_lookup_fn)(uint32_t)(*(uint16_t *)BOOTROM_TABLE_LOOKUP_OFFSET);
    return lookup(code, RT_FLAG_FUNC_ARM_SEC);
}
#pragma GCC diagnostic pop

typedef void (*rom_connect_internal_flash_fn)(void);
typedef void (*rom_flash_exit_xip_fn)(void);
typedef void (*rom_flash_range_erase_fn)(uint32_t addr, uint32_t count, uint32_t block_size, uint8_t block_erase_cmd);
typedef void (*rom_flash_range_program_fn)(uint32_t addr, const uint8_t *data, uint32_t count);
typedef void (*rom_flash_flush_cache_fn)(void);
typedef void (*rom_flash_enter_cmd_xip_fn)(void);

#define FLASH_BLOCK_SIZE      65536u
#define FLASH_BLOCK_ERASE_CMD 0xd8u

/* Same flash-unreadable-mid-erase hazard as RP2040 -- see src_2040/flash.c's
 * comment. Placed in RAM via linker_rp2350.ld's .ram_func section, copied
 * out at boot by crt0_rp2350.s's Reset_Handler. */
#define RAM_FUNC __attribute__((noinline, section(".ram_func")))

static rom_connect_internal_flash_fn connect_internal_flash;
static rom_flash_exit_xip_fn         rom_flash_exit_xip;
static rom_flash_range_erase_fn      rom_flash_range_erase;
static rom_flash_range_program_fn    rom_flash_range_program;
static rom_flash_flush_cache_fn      rom_flash_flush_cache;
static rom_flash_enter_cmd_xip_fn    rom_flash_enter_cmd_xip;

RAM_FUNC void flash_init(void) {
    connect_internal_flash  = (rom_connect_internal_flash_fn)rom_func_lookup(ROM_TABLE_CODE('I', 'F'));
    rom_flash_exit_xip      = (rom_flash_exit_xip_fn)rom_func_lookup(ROM_TABLE_CODE('E', 'X'));
    rom_flash_range_erase   = (rom_flash_range_erase_fn)rom_func_lookup(ROM_TABLE_CODE('R', 'E'));
    rom_flash_range_program = (rom_flash_range_program_fn)rom_func_lookup(ROM_TABLE_CODE('R', 'P'));
    rom_flash_flush_cache   = (rom_flash_flush_cache_fn)rom_func_lookup(ROM_TABLE_CODE('F', 'C'));
    rom_flash_enter_cmd_xip = (rom_flash_enter_cmd_xip_fn)rom_func_lookup(ROM_TABLE_CODE('C', 'X'));

    connect_internal_flash();
    rom_flash_exit_xip();
    rom_flash_flush_cache();
    rom_flash_enter_cmd_xip();
}

RAM_FUNC void flash_erase(uint32_t offset, uint32_t count) {
    connect_internal_flash();
    rom_flash_exit_xip();
    rom_flash_range_erase(offset, count, FLASH_BLOCK_SIZE, FLASH_BLOCK_ERASE_CMD);
    rom_flash_flush_cache();
    rom_flash_enter_cmd_xip();
}

RAM_FUNC void flash_program(uint32_t offset, const uint8_t *data, uint32_t count) {
    connect_internal_flash();
    rom_flash_exit_xip();
    rom_flash_range_program(offset, data, count);
    rom_flash_flush_cache();
    rom_flash_enter_cmd_xip();
}

const uint8_t *flash_read_ptr(uint32_t offset) {
    return (const uint8_t *)(FLASH_XIP_BASE + offset);
}
