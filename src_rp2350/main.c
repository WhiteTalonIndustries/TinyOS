#include <stdint.h>
#include <string.h>
#include "usb.h"

/* Stage 2: minimal shell over the USB CDC-ACM console, same UX as the
 * RP2040 build's src_2040/main.c but without filesystem-backed commands --
 * those need flash.c/fs.c ported first (Stage 3). `ls`/`cat`/`write`/etc.
 * will be added back once that lands. */

static char *next_token(char **cursor) {
    char *start;
    while (**cursor == ' ') (*cursor)++;
    if (**cursor == '\0') return NULL;
    start = *cursor;
    while (**cursor && **cursor != ' ') (*cursor)++;
    if (**cursor == ' ') { **cursor = '\0'; (*cursor)++; }
    return start;
}

static void shell_execute(char *cmd_line) {
    char *cursor = cmd_line;
    char *cmd = next_token(&cursor);

    if (!cmd) return;

    if (strcmp(cmd, "help") == 0) {
        console_puts("Commands: help, sysinfo, clear, hello, exit\n"
                      "(filesystem commands land in Stage 3 once flash.c is ported)\n");
    } else if (strcmp(cmd, "sysinfo") == 0) {
        console_puts("OS: TinyOS RP2350 port -- Stage 2 (USB console)\nCPU: Arm Cortex-M33 (RP2350)\nRAM: 520 KB\nFlash: 16 MB (not yet mounted)\n");
    } else if (strcmp(cmd, "hello") == 0) {
        console_puts("Hello from RP2350!\n");
    } else if (strcmp(cmd, "clear") == 0) {
        console_puts("\033[2J\033[H");
    } else if (strcmp(cmd, "exit") == 0) {
        console_puts("Closing console session...\n");
        console_disconnect();
    } else {
        console_puts("err: unknown instruction: ");
        console_puts(cmd);
        console_puts("\n");
    }
}

#define REG32(addr) (*(volatile uint32_t *)(addr))

/* Diagnostic-only: isolate whether main() is reached and basic GPIO works
 * at all in this build, independent of usb.c's led_init()/led_blink() --
 * identical register sequence to the Stage 1 main.c that was proven working
 * on this exact board. Blinks 5 times at ~1Hz (matching the just-confirmed
 * working sanity test's cadence) rather than once -- a single one-shot
 * blink proved too easy to miss/misjudge over chat; an ongoing repeating
 * pattern for a few seconds is much harder to mis-observe. */
static void raw_diagnostic_blink(void) {
    volatile uint32_t n;
    int i;

    REG32(0x40020000u) |= (1u << 6) | (1u << 9);  /* RESETS_RESET |= IO_BANK0|PADS_BANK0 */
    REG32(0x40020000u) &= ~((1u << 6) | (1u << 9));
    while (!(REG32(0x40020008u) & ((1u << 6) | (1u << 9)))); /* RESETS_RESET_DONE */

    REG32(0x40038034u) = 0u;      /* PADS_BANK0 GPIO13: clear ISO/OD */
    REG32(0x4002806cu) = 5u;      /* IO_BANK0 GPIO13_CTRL: funcsel = SIO */
    REG32(0xd0000038u) = (1u << 13); /* SIO GPIO_OE_SET */

    for (i = 0; i < 5; i++) {
        REG32(0xd0000018u) = (1u << 13); /* GPIO_OUT_SET: on */
        for (n = 3000000u; n; n--);
        REG32(0xd0000020u) = (1u << 13); /* GPIO_OUT_CLR: off */
        for (n = 3000000u; n; n--);
    }
}

int main(void) {
    char input_buffer[512];
    int char_count = 0;

    raw_diagnostic_blink(); /* checkpoint 0: main() reached, basic GPIO works */

    console_init();

    console_puts("\033[2J\033[H");
    console_puts("===========================================\n");
    console_puts("  TinyOS RP2350 Port -- Stage 2 (Pico Plus 2 W)\n");
    console_puts("===========================================\n");
    console_puts("Welcome! Type 'help' to view system tasks.\n\n");

    while (1) {
        console_puts("rp2350_sh$ ");
        char_count = 0;

        while (1) {
            char incoming = console_getc();

            if (incoming == '\r' || incoming == '\n') {
                console_puts("\n");
                input_buffer[char_count] = '\0';
                break;
            } else if (incoming == '\b' || incoming == 127) {
                if (char_count > 0) {
                    char_count--;
                    console_puts("\b \b");
                }
            } else if (char_count < 511) {
                input_buffer[char_count++] = incoming;
                console_putc(incoming);
            }
        }

        shell_execute(input_buffer);
    }
}
