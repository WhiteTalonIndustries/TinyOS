@ Second-stage bootloader: configures a Winbond W25Q-family QSPI flash chip
@ (also compatible with several similar parts: W25Q16JV, AT25SF081,
@ S25FL132K0) for continuous quad-I/O XIP reads, then jumps into the vector
@ table at flash offset 0x100.
@
@ The RP2040 boot ROM always loads and runs this from a fixed 256-byte
@ region at the start of flash on a cold boot (unless BOOTSEL is held), and
@ passes lr=0 to signal "jump into the app when done". Must be
@ position-independent and use the stack only -- the flash isn't in a
@ readable state until this code finishes.
@
@ This is a flattened, dependency-free translation of Raspberry Pi's
@ boot2_w25q080.S (BSD-3-Clause), with all register offsets/field values
@ inlined as literals instead of pulled from SDK headers, to keep this
@ project free of any pico-sdk build dependency. Values were checked against
@ the SDK's hardware/regs headers, not reconstructed from memory.

.syntax unified
.cpu cortex-m0plus
.thumb

.section .text

.global _stage2_boot
.thumb_func
_stage2_boot:
    push {lr}

    @ ---- QSPI pad config: 8mA drive + fast slew on SCLK, disable input
    @ ---- Schmitt trigger on the data lines to cut input delay ----
    ldr r3, =0x40020000            @ PADS_QSPI_BASE
    movs r0, #0x21                 @ DRIVE=2 (8mA) << 4 | SLEWFAST
    str r0, [r3, #0x04]            @ GPIO_QSPI_SCLK
    ldr r0, [r3, #0x08]            @ GPIO_QSPI_SD0
    movs r1, #0x02                 @ SCHMITT bit
    bics r0, r1
    str r0, [r3, #0x08]
    str r0, [r3, #0x0c]            @ SD1
    str r0, [r3, #0x10]            @ SD2
    str r0, [r3, #0x14]            @ SD3

    ldr r3, =0x18000000            @ XIP_SSI_BASE

    movs r1, #0
    str r1, [r3, #0x08]            @ SSIENR = 0 (disable to allow config)

    movs r1, #4                    @ clk_sys / 4 -- conservative but simple
    str r1, [r3, #0x14]            @ BAUDR

    movs r1, #1
    movs r2, #0xf0                 @ RX_SAMPLE_DLY offset
    str r1, [r3, r2]

    @ ---- Enable quad mode via a status-register write, if not already set ----
program_sregs:
    ldr r1, =0x00070000            @ CTRLR0: DFS=8 bits, TMOD=tx-and-rx
    str r1, [r3, #0x00]

    movs r1, #1
    str r1, [r3, #0x08]            @ SSIENR = 1

    movs r0, #0x35                 @ Read Status Register 2
    bl read_flash_sreg
    movs r2, #0x02                 @ QE (quad enable) bit
    cmp r0, r2
    beq skip_sreg_programming

    movs r1, #0x06                 @ Write Enable
    str r1, [r3, #0x60]            @ DR0
    bl wait_ssi_ready
    ldr r1, [r3, #0x60]

    movs r1, #0x01                 @ Write Status Register (1-byte SR1 + SR2)
    str r1, [r3, #0x60]
    movs r0, #0
    str r0, [r3, #0x60]            @ SR1 = 0
    str r2, [r3, #0x60]            @ SR2 = QE

    bl wait_ssi_ready
    ldr r1, [r3, #0x60]
    ldr r1, [r3, #0x60]
    ldr r1, [r3, #0x60]

poll_status:
    movs r0, #0x05                 @ Read Status Register 1
    bl read_flash_sreg
    movs r1, #1
    tst r0, r1                     @ BUSY bit
    bne poll_status

skip_sreg_programming:
    movs r1, #0
    str r1, [r3, #0x08]            @ SSIENR = 0

    @ ---- Dummy Fast Read Quad I/O, with continuation mode bits set, so
    @ ---- later reads don't need the 0xEB command prefix ----
dummy_read:
    ldr r1, =0x005f0300            @ CTRLR0: quad frf, 32 data bits, eeprom-read tmod
    str r1, [r3, #0x00]

    movs r1, #0
    str r1, [r3, #0x04]            @ CTRLR1 = 0 (single transfer)

    ldr r1, =0x00002221            @ SPI_CTRLR0: addr+mode=8, wait=4, inst_l=8bit, 1c2a
    ldr r0, =0x180000f4            @ XIP_SSI_BASE + SPI_CTRLR0_OFFSET
    str r1, [r0]

    movs r1, #1
    str r1, [r3, #0x08]            @ SSIENR = 1

    movs r1, #0xeb                 @ Fast Read Quad I/O
    str r1, [r3, #0x60]
    movs r1, #0xa0                 @ mode bits: continuation = 0xA
    str r1, [r3, #0x60]

    bl wait_ssi_ready

    movs r1, #0
    str r1, [r3, #0x08]            @ SSIENR = 0

    @ ---- Configure SSI to translate ordinary bus reads into continuous
    @ ---- quad-I/O flash transfers from here on ----
configure_ssi:
    ldr r1, =0xa0002022             @ SPI_CTRLR0: xip_cmd=mode bits, addr+mode=8, wait=4, no inst, 2c2a
    ldr r0, =0x180000f4
    str r1, [r0]

    movs r1, #1
    str r1, [r3, #0x08]            @ SSIENR = 1

    @ ---- Flash is now readable via the XIP window. Hand off. ----
check_return:
    pop {r0}
    cmp r0, #0
    beq vector_into_flash
    bx r0
vector_into_flash:
    ldr r0, =0x10000100            @ XIP_BASE + 0x100 (our vector table)
    ldr r1, =0xe000ed08             @ PPB_BASE + M0PLUS_VTOR_OFFSET
    str r0, [r1]
    ldmia r0, {r0, r1}
    msr msp, r0
    bx r1

wait_ssi_ready:
    push {r0, r1, lr}
1:
    ldr r1, [r3, #0x28]            @ SSI_SR
    movs r0, #0x04                 @ TFE (tx fifo empty)
    tst r1, r0
    beq 1b
    movs r0, #0x01                 @ BUSY
    tst r1, r0
    bne 1b
    pop {r0, r1, pc}

.thumb_func
read_flash_sreg:
    push {r1, lr}
    str r0, [r3, #0x60]            @ command byte
    str r0, [r3, #0x60]            @ dummy byte to clock out the response
    bl wait_ssi_ready
    ldr r0, [r3, #0x60]            @ discard echo of the command byte
    ldr r0, [r3, #0x60]            @ the actual status byte
    pop {r1, pc}

.ltorg
