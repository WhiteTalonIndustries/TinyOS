#ifndef TINYOS_STATUS_H
#define TINYOS_STATUS_H

/* Solid-color status codes shown on the display -- the debugging-output
 * mechanism this display was brought up for in the first place. Each
 * subsystem being brought up (USB, filesystem, etc.) gets its own code so a
 * glance at the screen says how far boot got, without needing a serial
 * connection. Extend this enum as new subsystems come online; keep codes
 * visually distinct (see status.c's color table) since that's the whole
 * point. */
typedef enum {
    STATUS_BOOTING = 0,   /* white: just started, nothing brought up yet */
    STATUS_LCD_OK,        /* green: display itself confirmed working */
    STATUS_USB_WAITING,   /* yellow: USB bring-up started, not yet enumerated */
    STATUS_USB_OK,        /* blue: USB enumerated, console usable */
    STATUS_ERROR          /* red: something failed -- see whichever subsystem was last attempted */
} tinyos_status_t;

void status_show(tinyos_status_t status);

#endif
