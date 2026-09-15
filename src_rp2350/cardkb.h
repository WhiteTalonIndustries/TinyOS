#ifndef TINYOS_CARDKB_H
#define TINYOS_CARDKB_H

/* M5Stack CardKB, connected via the board's onboard STEMMA QT/Qwiic
 * connector (I2C0, GPIO4/5 -- see PICO_DEFAULT_I2C_SDA_PIN/
 * PICO_DEFAULT_I2C_SCL_PIN in pico-sdk's board header for this exact
 * board). Registers itself as an additional stdio input source (same
 * fan-out mechanism usb.c/lcd_console.c already use), so every existing
 * console_getc()/getchar() call site -- the shell prompt, nano, script.c
 * -- picks up CardKB keystrokes with zero changes, indistinguishable
 * from typing over the USB CDC console.
 *
 * Ground-truthed against M5Stack's own docs/example
 * (https://docs.m5stack.com/en/unit/cardkb,
 * https://github.com/m5stack/M5Stack/blob/master/examples/Unit/CardKB/CardKB.ino)
 * plus a from-scratch CardKB-protocol emulator that documents the full
 * code table (https://github.com/jeroavf/Cardkb_emulator): I2C address
 * 0x5F, single-byte poll (0x00 = no key, nonzero = key value), plain
 * ASCII for regular keys/Enter(0x0D)/Backspace(0x08), and non-ASCII
 * 0xB4-0xB7 for the Fn+arrow combos -- not guessed from a generic
 * keyboard-IC datasheet. Safe to call even with nothing plugged into the
 * STEMMA QT port yet: reads just time out and are treated as "no key". */
void cardkb_init(void);

#endif
