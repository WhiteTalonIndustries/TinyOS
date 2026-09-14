@ RP2350 RAM/flash-common vector table + reset handler.
@
@ Unlike the RP2040 build (src_2040/crt0.s, Cortex-M0+), the RP2350's
@ Cortex-M33 is an Armv8-M core: it requires real handler slots for
@ MemManage/BusFault/UsageFault/SecureFault (M0+ has none of these — they
@ were reserved words there) or the core hard-faults on an unhandled
@ exception before it can even report why. IRQ numbering also differs
@ completely from RP2040 (see RP2350 datasheet table 95) — listed in full
@ below so later stages (USB, SPI, etc.) know which slot to wire up.

.syntax unified
.cpu cortex-m33
.thumb

.section .vectors, "ax"
.align 2
.global __vectors
__vectors:
    .word _estack             @  0. Initial stack pointer (top of RP2350 520KB SRAM)
    .word Reset_Handler       @  1. Reset
    .word Default_Handler     @  2. NMI
    .word HardFault_Handler   @  3. HardFault
    .word Default_Handler     @  4. MemManage
    .word Default_Handler     @  5. BusFault
    .word Default_Handler     @  6. UsageFault
    .word Default_Handler     @  7. SecureFault
    .word 0                   @  8. Reserved
    .word 0                   @  9. Reserved
    .word 0                   @ 10. Reserved
    .word Default_Handler     @ 11. SVCall
    .word Default_Handler     @ 12. DebugMon
    .word 0                   @ 13. Reserved
    .word Default_Handler     @ 14. PendSV
    .word Default_Handler     @ 15. SysTick
    .word Default_Handler     @ IRQ  0. TIMER0_IRQ_0
    .word Default_Handler     @ IRQ  1. TIMER0_IRQ_1
    .word Default_Handler     @ IRQ  2. TIMER0_IRQ_2
    .word Default_Handler     @ IRQ  3. TIMER0_IRQ_3
    .word Default_Handler     @ IRQ  4. TIMER1_IRQ_0
    .word Default_Handler     @ IRQ  5. TIMER1_IRQ_1
    .word Default_Handler     @ IRQ  6. TIMER1_IRQ_2
    .word Default_Handler     @ IRQ  7. TIMER1_IRQ_3
    .word Default_Handler     @ IRQ  8. PWM_IRQ_WRAP_0
    .word Default_Handler     @ IRQ  9. PWM_IRQ_WRAP_1
    .word Default_Handler     @ IRQ 10. DMA_IRQ_0
    .word Default_Handler     @ IRQ 11. DMA_IRQ_1
    .word Default_Handler     @ IRQ 12. DMA_IRQ_2
    .word Default_Handler     @ IRQ 13. DMA_IRQ_3
    .word USBCTRL_Handler     @ IRQ 14. USBCTRL_IRQ
    .word Default_Handler     @ IRQ 15. PIO0_IRQ_0
    .word Default_Handler     @ IRQ 16. PIO0_IRQ_1
    .word Default_Handler     @ IRQ 17. PIO1_IRQ_0
    .word Default_Handler     @ IRQ 18. PIO1_IRQ_1
    .word Default_Handler     @ IRQ 19. PIO2_IRQ_0
    .word Default_Handler     @ IRQ 20. PIO2_IRQ_1
    .word Default_Handler     @ IRQ 21. IO_IRQ_BANK0
    .word Default_Handler     @ IRQ 22. IO_IRQ_BANK0_NS
    .word Default_Handler     @ IRQ 23. IO_IRQ_QSPI
    .word Default_Handler     @ IRQ 24. IO_IRQ_QSPI_NS
    .word Default_Handler     @ IRQ 25. SIO_IRQ_FIFO
    .word Default_Handler     @ IRQ 26. SIO_IRQ_BELL
    .word Default_Handler     @ IRQ 27. SIO_IRQ_FIFO_NS
    .word Default_Handler     @ IRQ 28. SIO_IRQ_BELL_NS
    .word Default_Handler     @ IRQ 29. SIO_IRQ_MTIMECMP
    .word Default_Handler     @ IRQ 30. CLOCKS_IRQ
    .word Default_Handler     @ IRQ 31. SPI0_IRQ
    .word Default_Handler     @ IRQ 32. SPI1_IRQ
    .word Default_Handler     @ IRQ 33. UART0_IRQ
    .word Default_Handler     @ IRQ 34. UART1_IRQ
    .word Default_Handler     @ IRQ 35. ADC_IRQ_FIFO
    .word Default_Handler     @ IRQ 36. I2C0_IRQ
    .word Default_Handler     @ IRQ 37. I2C1_IRQ
    .word Default_Handler     @ IRQ 38. OTP_IRQ
    .word Default_Handler     @ IRQ 39. TRNG_IRQ
    .word Default_Handler     @ IRQ 40. PROC0_IRQ_CTI
    .word Default_Handler     @ IRQ 41. PROC1_IRQ_CTI
    .word Default_Handler     @ IRQ 42. PLL_SYS_IRQ
    .word Default_Handler     @ IRQ 43. PLL_USB_IRQ
    .word Default_Handler     @ IRQ 44. POWMAN_IRQ_POW
    .word Default_Handler     @ IRQ 45. POWMAN_IRQ_TIMER
    .word Default_Handler     @ IRQ 46-51. SPAREIRQ (never fire on RP2350)
    .word Default_Handler
    .word Default_Handler
    .word Default_Handler
    .word Default_Handler
    .word Default_Handler

.section .text
.global Reset_Handler
.type Reset_Handler, %function
Reset_Handler:
    cpsid i                   @ Disable interrupts during initialization

    @ Point VTOR at our vector table, regardless of what the bootrom left it as
    ldr r0, =0xE000ED08
    ldr r1, =__vectors
    str r1, [r0]

    @ Copy .data's initial values out of flash and into RAM
    ldr r0, =_sidata
    ldr r1, =_sdata
    ldr r2, =_edata
data_loop:
    cmp r1, r2
    bhs data_done
    ldr r4, [r0]
    str r4, [r1]
    adds r0, r0, #4
    adds r1, r1, #4
    b data_loop
data_done:

    @ Clear BSS memory section
    ldr r0, =_sbss
    ldr r1, =_ebss
    movs r2, #0
bss_loop:
    cmp r0, r1
    bhs bss_done
    str r2, [r0]
    adds r0, r0, #4
    b bss_loop
bss_done:

    cpsie i                   @ Enable interrupts
    bl main                   @ Launch the C OS kernel

hang:
    b hang

@ Diagnostic-build fault handler: fast, distinctive blink on GPIO13 (the
@ display backlight, same pin main.c/usb.c use for boot-progress signaling)
@ so a genuine CPU fault is visually distinguishable from a plain infinite
@ loop elsewhere in C code. Written in raw asm, touching only registers, so
@ it works no matter how early the fault happens or what state C globals
@ are in -- it redoes the pad/funcsel/OE setup itself rather than assuming
@ led_init() already ran.
.thumb_func
HardFault_Handler:
    ldr r0, =0x40038034     @ PADS_BANK0_BASE + 0x04 + 13*4 (GPIO13 pad ctrl)
    movs r1, #0
    str r1, [r0]            @ clear ISO/OD

    ldr r0, =0x4002806c     @ IO_BANK0_BASE + 0x04 + 13*8 (GPIO13_CTRL)
    movs r1, #5
    str r1, [r0]            @ funcsel = SIO

    ldr r0, =0xd0000038     @ SIO_BASE + GPIO_OE_SET
    ldr r1, =0x00002000     @ 1 << 13
    str r1, [r0]

    ldr r2, =0xd0000018     @ SIO_BASE + GPIO_OUT_SET
    ldr r3, =0xd0000020     @ SIO_BASE + GPIO_OUT_CLR
fault_blink:
    str r1, [r2]
    ldr r4, =200000
fault_delay1:
    subs r4, r4, #1
    bne fault_delay1
    str r1, [r3]
    ldr r4, =200000
fault_delay2:
    subs r4, r4, #1
    bne fault_delay2
    b fault_blink

.thumb_func
Default_Handler:   b HardFault_Handler
.thumb_func
.weak USBCTRL_Handler
USBCTRL_Handler:   b hang
