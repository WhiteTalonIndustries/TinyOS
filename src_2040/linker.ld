MEMORY
{
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 264K
}

ENTRY(Reset_Handler)

SECTIONS
{
    .text : {
        KEEP(*(.vectors))
        *(.text*)
        *(.rodata*)
    } > RAM

    .data : {
        *(.data*)
    } > RAM

    /* flash.c marks a few functions RAM-resident (see its RAM_FUNC macro)
     * for the flash-boot build's sake -- meaningless distinction here since
     * this whole image already runs from RAM, but needs a landing spot. */
    .ram_func : {
        *(.ram_func)
    } > RAM

    .bss : {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        _ebss = .;
    } > RAM

    _estack = ORIGIN(RAM) + LENGTH(RAM);
}
