#ifndef TINYOS_EDITOR_H
#define TINYOS_EDITOR_H

/* Full-screen line editor over the serial console: arrow keys, backspace,
 * Ctrl+S to save, Ctrl+X to exit. Not real nano, but nano-shaped. */
void nano_edit(const char *filename);

#endif
