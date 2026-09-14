/* RP2350 flash-boot linker script (Pimoroni Pico Plus 2 W: 16MB W25Q128JV flash,
 * 520KB SRAM). Unlike src_2040/linker_flash.ld, there is no boot2 stage to skip
 * past at flash offset 0 — RP2350's boot ROM finds the picobin metadata block
 * itself (see picobin_block.s) and sets up XIP before jumping to the reset
 * vector, so the vector table can start right at the flash base. The metadata
 * block must land within the flash image's first 4KB, so it's placed
 * immediately after the vector table, ahead of all other code. */

MEMORY
{
    FLASH (rx) : ORIGIN = 0x10000000, LENGTH = 16M
    RAM   (rwx): ORIGIN = 0x20000000, LENGTH = 520K
}

SECTIONS
{
    .flash_bootmeta ORIGIN(FLASH) :
    {
        KEEP(*(.vectors))
        KEEP(*(.picobin_block))
    } > FLASH

    .text :
    {
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT> FLASH

    /* Functions that must never be executing from flash while flash itself
     * is unreadable (mid-erase/program) -- see flash.c. Copied to RAM by
     * crt0_rp2350.s's Reset_Handler, same as src_2040/linker_flash.ld. */
    .ram_func :
    {
        . = ALIGN(4);
        _sramfunc = .;
        *(.ram_func)
        . = ALIGN(4);
        _eramfunc = .;
    } > RAM AT> FLASH
    _siramfunc = LOADADDR(.ram_func);

    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    . = ALIGN(8);
    _estack = ORIGIN(RAM) + LENGTH(RAM);
}
