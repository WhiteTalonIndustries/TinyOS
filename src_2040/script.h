#ifndef TINYOS_SCRIPT_H
#define TINYOS_SCRIPT_H

/* Runs a C-like script: integers/strings, if/while, function defs with
 * params, and a small set of native functions (print, write, read, exists,
 * len) that call straight into the filesystem/console. */
void script_run(const char *source);

#endif
