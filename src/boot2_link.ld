/* Minimal linker script just for compiling boot2.S in isolation to a flat
 * 240-byte binary (before pad_checksum.py pads it to 256). The origin here
 * doesn't need to match its eventual flash address -- the code is fully
 * position-independent (stack-only, no absolute self-references). */
MEMORY { FLASH(rx) : ORIGIN = 0, LENGTH = 256 }
SECTIONS { . = ORIGIN(FLASH); .text : { *(.text) } > FLASH }
