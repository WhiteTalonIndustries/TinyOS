#include "LCD_Driver.h"
#include "DEV_Config.h"
#include "status.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

/* USB CDC-ACM console, built on pico-sdk's stdio_usb (TinyUSB underneath)
 * rather than a bare-metal USB device-controller port -- see
 * README_RP2350.md for why. Display shows status_show() codes for how far
 * boot got: white while USB is waiting for a host, blue once connected. */

#define INPUT_BUF_SIZE 256

static void shell_execute(char *cmd_line) {
    if (cmd_line[0] == '\0') return;

    if (strcmp(cmd_line, "help") == 0) {
        printf("Commands: help, hello, status\n");
    } else if (strcmp(cmd_line, "hello") == 0) {
        printf("Hello from RP2350 (pico-sdk build)!\n");
    } else if (strcmp(cmd_line, "status") == 0) {
        printf("USB: connected. Display: OK.\n");
    } else {
        printf("err: unknown command: %s\n", cmd_line);
    }
}

int main(void) {
    System_Init();
    LCD_Init(SCAN_DIR_DFT, 800);
    status_show(STATUS_LCD_OK);

    stdio_init_all();
    status_show(STATUS_USB_WAITING);

    while (!stdio_usb_connected()) {
        tight_loop_contents();
    }
    status_show(STATUS_USB_OK);

    printf("\n===========================================\n");
    printf("  TinyOS RP2350 -- pico-sdk build\n");
    printf("===========================================\n");
    printf("Welcome! Type 'help' to view commands.\n\n");

    char input_buffer[INPUT_BUF_SIZE];
    while (1) {
        printf("rp2350_sh$ ");
        fflush(stdout);

        int len = 0;
        while (1) {
            int c = getchar();
            if (c == '\r' || c == '\n') {
                printf("\n");
                input_buffer[len] = '\0';
                break;
            } else if ((c == '\b' || c == 127) && len > 0) {
                len--;
                printf("\b \b");
                fflush(stdout);
            } else if (c >= 0x20 && c < 0x7f && len < INPUT_BUF_SIZE - 1) {
                input_buffer[len++] = (char)c;
                putchar(c);
                fflush(stdout);
            }
        }

        shell_execute(input_buffer);
    }
}
