#include <stdint.h>
#include "flash.h"

/* ================= RP2040 boot ROM function lookup =================
 * The boot ROM (present in silicon, independent of any SDK) exposes a
 * small set of helper functions -- including the flash erase/program
 * routines -- via a lookup table found through two fixed 16-bit pointers
 * near address 0. See RP2040 datasheet 2.8 "Bootrom". */

#define ROM_TABLE_CODE(c1, c2) ((uint32_t)(uint8_t)(c1) | ((uint32_t)(uint8_t)(c2) << 8))

#define BOOTROM_FUNC_TABLE_OFFSET   0x14u
#define BOOTROM_TABLE_LOOKUP_OFFSET 0x18u

typedef void *(*rom_table_lookup_fn)(uint16_t *table, uint32_t code);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
static void *rom_hword_as_ptr(uint32_t rom_address) {
    return (void *)(uint32_t)(*(uint16_t *)rom_address);
}
#pragma GCC diagnostic pop

static void *rom_func_lookup(uint32_t code) {
    rom_table_lookup_fn lookup = (rom_table_lookup_fn)rom_hword_as_ptr(BOOTROM_TABLE_LOOKUP_OFFSET);
    uint16_t *func_table = (uint16_t *)rom_hword_as_ptr(BOOTROM_FUNC_TABLE_OFFSET);
    return lookup(func_table, code);
}

typedef void (*rom_connect_internal_flash_fn)(void);
typedef void (*rom_flash_exit_xip_fn)(void);
typedef void (*rom_flash_range_erase_fn)(uint32_t addr, uint32_t count, uint32_t block_size, uint8_t block_erase_cmd);
typedef void (*rom_flash_range_program_fn)(uint32_t addr, const uint8_t *data, uint32_t count);
typedef void (*rom_flash_flush_cache_fn)(void);
typedef void (*rom_flash_enter_cmd_xip_fn)(void);

#define FLASH_BLOCK_SIZE      65536u
#define FLASH_BLOCK_ERASE_CMD 0xd8u

/* Flash is unreadable for the stretch between exit_xip() and
 * enter_cmd_xip() below. In the RAM-resident build that's harmless (no code
 * or data lives in flash), but in the flash-boot build the kernel itself
 * executes from flash -- so any function with flash-unreadable code in its
 * body must run from RAM instead, or the CPU loses its own instruction
 * stream mid-erase. noinline keeps the linker's ".ram_func" placement
 * meaningful instead of being silently folded into a flash-resident caller. */
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

    /* Bring flash from an unknown state (BOOTSEL entry never touches the
     * QSPI pins) into simple command-mode XIP, so flash_read_ptr() works. */
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
