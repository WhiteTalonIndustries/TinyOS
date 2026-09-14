#include <stdint.h>
#include <string.h>
#include "usb.h"
#include "flash.h"
#include "fs.h"
#include "editor.h"
#include "script.h"
#include "adc.h"

/* Stage 3: full 2040-parity shell over the USB CDC-ACM console -- same
 * command surface as src_2040/main.c (fs, nano editor, script runner, ADC
 * randomness), now that flash.c/fs.c/editor.c/script.c/adc.c are all ported.
 * USB itself still doesn't enumerate on real hardware (see README_RP2350.md
 * "Known issue") -- this is written and buildable, but untested end-to-end. */

static char catbuf[4096];

static void print_udec(uint32_t v) {
    char tmp[10];
    int n = 0;
    if (v == 0) { console_putc('0'); return; }
    while (v > 0 && n < 10) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    while (n > 0) console_putc(tmp[--n]);
}

static void print_entry(const char *name, int type, uint32_t length) {
    console_puts(type == FS_TYPE_DIR ? "d " : "- ");
    console_puts(name);
    if (type != FS_TYPE_DIR) {
        console_puts("  (");
        print_udec(length);
        console_puts(" bytes)");
    }
    console_puts("\n");
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
        console_puts("Commands: help, sysinfo, clear, hello, ls, cat <file>, write <file> <text>,\n"
                      "          mkdir <dir>, rm <name>, mv <old> <new>, nano <file>, run <file>,\n"
                      "          format, exit\n");
    } else if (strcmp(cmd, "sysinfo") == 0) {
        console_puts("OS: TinyOS RP2350 port -- Stage 3 (2040 parity)\nCPU: Arm Cortex-M33 (RP2350)\nRAM: 520 KB\nFlash: 16 MB (TinyFS)\n");
    } else if (strcmp(cmd, "hello") == 0) {
        console_puts("Hello from RP2350!\n");
    } else if (strcmp(cmd, "clear") == 0) {
        console_puts("\033[2J\033[H");
    } else if (strcmp(cmd, "ls") == 0) {
        fs_list(print_entry);
    } else if (strcmp(cmd, "cat") == 0) {
        arg1 = next_token(&cursor);
        if (!arg1) {
            console_puts("usage: cat <file>\n");
        } else {
            int n = fs_read(arg1, catbuf, sizeof(catbuf));
            if (n < 0) {
                console_puts("cat: no such file: ");
                console_puts(arg1);
                console_puts("\n");
            } else {
                console_write(catbuf, (unsigned int)n);
                console_puts("\n");
            }
        }
    } else if (strcmp(cmd, "write") == 0) {
        arg1 = next_token(&cursor);
        if (!arg1) {
            console_puts("usage: write <file> <text>\n");
        } else {
            arg2 = cursor;
            while (*arg2 == ' ') arg2++;
            if (fs_write(arg1, arg2) != 0)
                console_puts("write: failed (name too long, file table full, or out of space)\n");
        }
    } else if (strcmp(cmd, "mkdir") == 0) {
        arg1 = next_token(&cursor);
        if (!arg1) console_puts("usage: mkdir <name>\n");
        else if (fs_mkdir(arg1) != 0) console_puts("mkdir: failed (already exists, name too long, or table full)\n");
    } else if (strcmp(cmd, "rm") == 0) {
        arg1 = next_token(&cursor);
        if (!arg1) {
            console_puts("usage: rm <name>\n");
        } else if (fs_remove(arg1) != 0) {
            console_puts("rm: no such file: ");
            console_puts(arg1);
            console_puts("\n");
        }
    } else if (strcmp(cmd, "mv") == 0) {
        arg1 = next_token(&cursor);
        arg2 = next_token(&cursor);
        if (!arg1 || !arg2) {
            console_puts("usage: mv <old> <new>\n");
        } else if (fs_rename(arg1, arg2) != 0) {
            console_puts("mv: no such file: ");
            console_puts(arg1);
            console_puts("\n");
        }
    } else if (strcmp(cmd, "nano") == 0) {
        arg1 = next_token(&cursor);
        if (!arg1) console_puts("usage: nano <file>\n");
        else nano_edit(arg1);
    } else if (strcmp(cmd, "run") == 0) {
        arg1 = next_token(&cursor);
        if (!arg1) {
            console_puts("usage: run <file>\n");
        } else {
            int n = fs_read(arg1, catbuf, sizeof(catbuf));
            if (n < 0) {
                console_puts("run: no such file: ");
                console_puts(arg1);
                console_puts("\n");
            } else {
                script_run(catbuf);
            }
        }
    } else if (strcmp(cmd, "format") == 0) {
        fs_format();
        console_puts("Filesystem formatted.\n");
    } else if (strcmp(cmd, "exit") == 0) {
        console_puts("Closing console session...\n");
        console_disconnect();
    } else {
        console_puts("err: unknown instruction: ");
        console_puts(cmd);
        console_puts("\n");
    }
}

int main(void) {
    char input_buffer[512];
    int char_count = 0;

    console_init();
    flash_init();
    fs_init();
    adc_init();

    console_puts("\033[2J\033[H");
    console_puts("===========================================\n");
    console_puts("  TinyOS RP2350 Port -- Stage 3 (Pico Plus 2 W)\n");
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
