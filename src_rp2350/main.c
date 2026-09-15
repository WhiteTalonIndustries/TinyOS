#include "LCD_Driver.h"
#include "DEV_Config.h"
#include "status.h"
#include "lcd_console.h"
#include "usb.h"
#include "flash.h"
#include "fs.h"
#include "editor.h"
#include "script.h"
#include "adc.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

/* 2040-parity shell, built on pico-sdk's stdio_usb instead of a bare-metal
 * USB device-controller port -- see README_RP2350.md. editor.c/script.c/
 * fs.c are reused verbatim from src_2040 (pure logic, no register access);
 * flash.c/adc.c/usb.c are pico-sdk-backed reimplementations of the same
 * interfaces. Command surface matches src_2040/main.c exactly. */

static char catbuf[4096];

static void print_entry(const char *name, int type, uint32_t length) {
    if (type == FS_TYPE_DIR) {
        printf("d %s\n", name);
    } else {
        printf("- %s  (%lu bytes)\n", name, (unsigned long)length);
    }
}

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
    char *arg1, *arg2;

    if (!cmd) return;

    if (strcmp(cmd, "help") == 0) {
        printf("Commands: help, sysinfo, clear, hello, ls, cat <file>, write <file> <text>,\n"
               "          mkdir <dir>, rm <name>, mv <old> <new>, nano <file>, run <file>,\n"
               "          format, exit\n");
    } else if (strcmp(cmd, "sysinfo") == 0) {
        printf("OS: TinyOS RP2350 (pico-sdk build)\nCPU: Arm Cortex-M33 (RP2350B)\nRAM: 520 KB\nFlash: 16 MB (TinyFS)\n");
    } else if (strcmp(cmd, "hello") == 0) {
        printf("Hello from RP2350!\n");
    } else if (strcmp(cmd, "clear") == 0) {
        printf("\033[2J\033[H");
    } else if (strcmp(cmd, "ls") == 0) {
        fs_list(print_entry);
    } else if (strcmp(cmd, "cat") == 0) {
        arg1 = next_token(&cursor);
        if (!arg1) {
            printf("usage: cat <file>\n");
        } else {
            int n = fs_read(arg1, catbuf, sizeof(catbuf));
            if (n < 0) {
                printf("cat: no such file: %s\n", arg1);
            } else {
                console_write(catbuf, (unsigned int)n);
                printf("\n");
            }
        }
    } else if (strcmp(cmd, "write") == 0) {
        arg1 = next_token(&cursor);
        if (!arg1) {
            printf("usage: write <file> <text>\n");
        } else {
            arg2 = cursor;
            while (*arg2 == ' ') arg2++;
            if (fs_write(arg1, arg2) != 0)
                printf("write: failed (name too long, file table full, or out of space)\n");
        }
    } else if (strcmp(cmd, "mkdir") == 0) {
        arg1 = next_token(&cursor);
        if (!arg1) printf("usage: mkdir <name>\n");
        else if (fs_mkdir(arg1) != 0) printf("mkdir: failed (already exists, name too long, or table full)\n");
    } else if (strcmp(cmd, "rm") == 0) {
        arg1 = next_token(&cursor);
        if (!arg1) {
            printf("usage: rm <name>\n");
        } else if (fs_remove(arg1) != 0) {
            printf("rm: no such file: %s\n", arg1);
        }
    } else if (strcmp(cmd, "mv") == 0) {
        arg1 = next_token(&cursor);
        arg2 = next_token(&cursor);
        if (!arg1 || !arg2) {
            printf("usage: mv <old> <new>\n");
        } else if (fs_rename(arg1, arg2) != 0) {
            printf("mv: no such file: %s\n", arg1);
        }
    } else if (strcmp(cmd, "nano") == 0) {
        arg1 = next_token(&cursor);
        if (!arg1) printf("usage: nano <file>\n");
        else nano_edit(arg1);
    } else if (strcmp(cmd, "run") == 0) {
        arg1 = next_token(&cursor);
        if (!arg1) {
            printf("usage: run <file>\n");
        } else {
            int n = fs_read(arg1, catbuf, sizeof(catbuf));
            if (n < 0) {
                printf("run: no such file: %s\n", arg1);
            } else {
                script_run(catbuf);
            }
        }
    } else if (strcmp(cmd, "format") == 0) {
        fs_format();
        printf("Filesystem formatted.\n");
    } else if (strcmp(cmd, "exit") == 0) {
        printf("Closing console session...\n");
        console_disconnect();
    } else {
        printf("err: unknown instruction: %s\n", cmd);
    }
}

int main(void) {
    System_Init();
    LCD_Init(U2D_R2L, 800); /* landscape, rotated 90 deg clockwise -- D2U_L2R was tried first but confirmed on hardware to rotate counter-clockwise instead */
    status_show(STATUS_LCD_OK);

    stdio_init_all();
    status_show(STATUS_USB_WAITING);
    while (!stdio_usb_connected()) {
        tight_loop_contents();
    }
    status_show(STATUS_USB_OK);

    /* From here on, everything printed to the USB console also renders on
     * the LCD -- register AFTER the status-code screens above so those
     * solid colors aren't immediately overwritten by the terminal's black
     * background. */
    lcd_console_init();
    lcd_console_register_stdio();

    flash_init();
    fs_init();
    tinyos_adc_init();

    printf("\033[2J\033[H");
    printf("===========================================\n");
    printf("  TinyOS RP2350 -- pico-sdk build\n");
    printf("===========================================\n");
    printf("Welcome! Type 'help' to view system tasks.\n\n");

    char input_buffer[512];
    while (1) {
        printf("rp2350_sh$ ");
        fflush(stdout);
        int char_count = 0;

        while (1) {
            char incoming = console_getc();

            if (incoming == '\r' || incoming == '\n') {
                printf("\n");
                input_buffer[char_count] = '\0';
                break;
            } else if (incoming == '\b' || incoming == 127) {
                if (char_count > 0) {
                    char_count--;
                    printf("\b \b");
                    fflush(stdout);
                }
            } else if (char_count < 511) {
                input_buffer[char_count++] = incoming;
                console_putc(incoming);
            }
        }

        shell_execute(input_buffer);
    }
}
