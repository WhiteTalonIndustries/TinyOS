@ RP2350 boot metadata block (replaces the RP2040 boot2.S checksum scheme).
@
@ RP2350's boot ROM does not run a 256-byte second-stage bootloader from flash
@ offset 0 like RP2040. Instead it scans the first 4KB of the flash image for
@ a "PICOBIN" metadata block and refuses to run anything that doesn't have
@ one. This is the minimum valid block for a plain executable image, per
@ RP2350 datasheet section 5.9.5.1 ("Minimum Arm IMAGE_DEF"): one block
@ containing an IMAGE_TYPE item and a LAST item, in a one-block loop (it
@ points at itself, meaning "no other blocks").
@
@ It must be linked into the first 4KB of the flash image (see the
@ .picobin_block placement in linker_rp2350.ld, right after the vector
@ table) — that's how the boot ROM finds it.

.syntax unified

.section .picobin_block, "a"
.align 2

.word 0xffffded3          @ PICOBIN_BLOCK_MARKER_START

@ --- item 0: IMAGE_TYPE ---
.byte 0x42                @ PICOBIN_BLOCK_ITEM_1BS_IMAGE_TYPE
.byte 0x01                @ item length: 1 word
.hword 0b0001000000100001 @ EXE, secure, ARM, RP2350 (see datasheet 5.9.2.1)

@ --- item 1: LAST ---
.byte 0xff                @ PICOBIN_BLOCK_ITEM_2BS_LAST
.hword 0x0001             @ item length: 1 word
.byte 0x00                @ pad

.word 0                   @ relative offset to next block: 0 = loop to self (no other blocks)
.word 0xab123579          @ PICOBIN_BLOCK_MARKER_END
