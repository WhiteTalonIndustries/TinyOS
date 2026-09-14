/* Linker script for the flash-resident (persists across power cycles) build.
 * FLASH starts 256 bytes into the chip -- that first 256 bytes is boot2,
 * prepended to the final image separately (see tasks.json), not placed by
 * this script. .data and .ram_func are stored in flash (LOADADDR) but run
 * from RAM; crt0_flash.s copies them out at boot before anything can call
 * into them. See fs.c's FS_BASE_OFFSET for where the filesystem begins. */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x10000100, LENGTH = 8192K - 256
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 264K
}

ENTRY(Reset_Handler)

SECTIONS
{
    .text : {
        KEEP(*(.vectors))
        *(.text*)
        *(.rodata*)
    } > FLASH

    .data : {
        _sdata = .;
        *(.data*)
        _edata = .;
    } > RAM AT> FLASH
    _sidata = LOADADDR(.data);

    /* Functions that must never be executing from flash while flash itself
     * is unreadable (mid-erase/program) -- see flash.c. */
    .ram_func : {
        _sramfunc = .;
        *(.ram_func)
        _eramfunc = .;
    } > RAM AT> FLASH
    _siramfunc = LOADADDR(.ram_func);

    .bss : {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        _ebss = .;
    } > RAM

    _estack = ORIGIN(RAM) + LENGTH(RAM);
}
