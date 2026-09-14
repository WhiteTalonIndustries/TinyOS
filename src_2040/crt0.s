.syntax unified
.cpu cortex-m0plus
.thumb

.section .vectors, "ax"
.align 2
.global __vectors
__vectors:
    .word _estack             @  0. Top of Stack (End of RP2040 264KB RAM)
    .word Reset_Handler       @  1. Reset Vector
    .word NMI_Handler         @  2. Non-Maskable Interrupt
    .word HardFault_Handler   @  3. Hard Fault Handler
    .word Default_Handler     @  4. Reserved
    .word Default_Handler     @  5. Reserved
    .word Default_Handler     @  6. Reserved
    .word Default_Handler     @  7. Reserved
    .word Default_Handler     @  8. Reserved
    .word Default_Handler     @  9. Reserved
    .word Default_Handler     @ 10. Reserved
    .word Default_Handler     @ 11. SVCall
    .word Default_Handler     @ 12. Reserved
    .word Default_Handler     @ 13. Reserved
    .word Default_Handler     @ 14. PendSV
    .word Default_Handler     @ 15. SysTick
    .word Default_Handler     @ 16. IRQ0  TIMER_IRQ_0
    .word Default_Handler     @ 17. IRQ1  TIMER_IRQ_1
    .word Default_Handler     @ 18. IRQ2  TIMER_IRQ_2
    .word Default_Handler     @ 19. IRQ3  TIMER_IRQ_3
    .word Default_Handler     @ 20. IRQ4  PWM_IRQ_WRAP
    .word USBCTRL_Handler     @ 21. IRQ5  USBCTRL_IRQ

.section .text
.global Reset_Handler
.type Reset_Handler, %function
Reset_Handler:
    cpsid i                   @ Disable interrupts during initialization

    @ Point VTOR at our vector table, regardless of what the bootrom left it as
    ldr r0, =0xE000ED08
    ldr r1, =__vectors
    str r1, [r0]

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

.thumb_func
NMI_Handler:       b hang
.thumb_func
HardFault_Handler: b hang
.thumb_func
Default_Handler:   b hang
.thumb_func
.weak USBCTRL_Handler
USBCTRL_Handler:   b hang
