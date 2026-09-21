#include "LCD_Driver.h"
#include "DEV_Config.h"
#include "status.h"
#include "lcd_console.h"
#include "usb.h"
#include "wifi.h"
#include "net.h"
#include "cardkb.h"
#include "fs.h"
#include "editor.h"
#include "script.h"
#include "adc.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

/* 2040-parity shell, built on a composite TinyUSB CDC+MSC device instead of
 * a bare-metal USB device-controller port -- see README_RP2350.md.
 * editor.c/script.c are reused verbatim from src_2040 (pure logic, no
 * register access); adc.c/usb.c are pico-sdk-backed reimplementations of
 * the same interfaces; fs.c is a from-scratch SD/FatFs-backed
 * implementation. "mount"/"unmount" hand the SD card to/from the USB host
 * as a raw block device (msc_disk.c) alongside the CDC console. wifi.c
 * brings up the CYW43 chip + lwIP (NO_SYS/polling, see lwipopts.h) from
 * WIFI.CFG on the SD card, but only on demand via "wifi connect" -- NOT
 * automatically at boot, since cyw43_arch_init() has caused a hard panic
 * when brought up unconditionally at startup on this board. "ifconfig"
 * reports connection state. Command surface otherwise matches
 * src_2040/main.c exactly. */

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
               "          cd <dir>, pwd, format, mount, unmount, wifi connect,\n"
               "          wifi disconnect, ifconfig, ping <host>, browser <host> [path], exit\n");
    } else if (strcmp(cmd, "sysinfo") == 0) {
        printf("OS: TinyOS RP2350 (pico-sdk build)\nCPU: Arm Cortex-M33 (RP2350B)\nRAM: 520 KB\nSystem flash: 16 MB\nUser storage: microSD (FAT)\n");
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
    } else if (strcmp(cmd, "cd") == 0) {
        arg1 = next_token(&cursor);
        if (!arg1) arg1 = "/";
        if (fs_chdir(arg1) != 0) printf("cd: no such directory: %s\n", arg1);
    } else if (strcmp(cmd, "pwd") == 0) {
        char cwd[128];
        if (fs_getcwd(cwd, sizeof(cwd)) == 0) printf("%s\n", cwd);
        else printf("pwd: failed\n");
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
    } else if (strcmp(cmd, "mount") == 0) {
        fs_usb_mount();
        printf("SD card handed to the USB host -- appears as a drive on the PC.\n"
               "TinyOS's own file commands are unavailable until 'unmount'.\n");
    } else if (strcmp(cmd, "unmount") == 0) {
        if (fs_usb_unmount() != 0) {
            printf("unmount: failed to remount the filesystem\n");
        } else {
            printf("SD card reclaimed from the USB host.\n");
        }
    } else if (strcmp(cmd, "wifi") == 0) {
        arg1 = next_token(&cursor);
        if (arg1 && strcmp(arg1, "connect") == 0) {
            wifi_init();
        } else if (arg1 && strcmp(arg1, "disconnect") == 0) {
            wifi_disconnect();
        } else {
            printf("usage: wifi connect | wifi disconnect\n");
        }
    } else if (strcmp(cmd, "ifconfig") == 0) {
        wifi_print_status();
    } else if (strcmp(cmd, "ping") == 0) {
        arg1 = next_token(&cursor);
        if (!arg1) printf("usage: ping <host>\n");
        else net_ping(arg1);
    } else if (strcmp(cmd, "browser") == 0) {
        arg1 = next_token(&cursor);
        if (!arg1) {
            printf("usage: browser <host> [path]\n");
        } else {
            arg2 = next_token(&cursor);
            net_get(arg1, arg2);
        }
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
    usb_init();
    status_show(STATUS_USB_WAITING); /* USB is now optional -- see below -- so this is shown only briefly */

    /* The board is fully usable standalone via the LCD + CardKB, so boot
     * no longer blocks here waiting for a USB terminal to connect (it
     * used to spin forever on `while (!usb_console_connected())`, which
     * meant the board was useless without a PC on the other end of the
     * cable). USB CDC keeps enumerating/connecting in the background --
     * tud_task() is still polled every time console_getc() calls
     * getchar() -- and cdc_stdio_out_chars() (usb.c) now skips writing
     * when nothing is connected instead of spinning on a full TX FIFO, so
     * printf() to the LCD-only console never blocks on an absent host. */
    status_show(STATUS_USB_OK);

    /* From here on, everything printed also renders on the LCD -- register
     * AFTER the status-code screens above so those solid colors aren't
     * immediately overwritten by the terminal's black background. */
    lcd_console_init();
    lcd_console_register_stdio();

    fs_init();
    tinyos_adc_init();
    cardkb_init(); /* CardKB over the onboard STEMMA QT port (I2C0) -- safe with nothing plugged in yet */
    /* WiFi stays off until explicitly requested (`wifi connect`) -- do NOT
     * call wifi_init() automatically at boot: cyw43_arch_init() has
     * caused a hard panic on this board when brought up unconditionally
     * at startup. */

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
