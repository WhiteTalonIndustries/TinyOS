#ifndef TINYOS_FLASH_H
#define TINYOS_FLASH_H

#include <stdint.h>

#define FLASH_PAGE_SIZE   256u
#define FLASH_SECTOR_SIZE 4096u
#define FLASH_XIP_BASE    0x10000000u

/* Looks up the boot ROM's flash helper functions and brings the QSPI flash
 * into a state where flash_read_ptr() works. Must be called before any
 * other flash_* function. */
void flash_init(void);

/* offset and count must both be multiples of FLASH_SECTOR_SIZE. */
void flash_erase(uint32_t offset, uint32_t count);

/* offset and count must both be multiples of FLASH_PAGE_SIZE. The target
 * range must already be erased (all 0xFF) — flash can only clear bits,
 * not set them, without an erase. */
void flash_program(uint32_t offset, const uint8_t *data, uint32_t count);

/* Direct memory-mapped read access into flash at the given byte offset. */
const uint8_t *flash_read_ptr(uint32_t offset);

#endif
