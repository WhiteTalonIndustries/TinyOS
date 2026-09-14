#ifndef TINYOS_FS_H
#define TINYOS_FS_H

#include <stdint.h>

#define FS_MAX_FILES  32
#define FS_NAME_LEN   24

#define FS_TYPE_FILE 0
#define FS_TYPE_DIR  1

typedef struct {
    uint32_t length;
} fs_stat_t;

/* Mounts the filesystem (formatting it fresh if no valid one is found on
 * flash). Must be called once at boot, after flash_init(). */
void fs_init(void);

/* Wipes the directory table and resets the data allocator. Does not erase
 * previously written file data, only forgets it. */
void fs_format(void);

/* Calls cb(name, type, length) once per entry, in directory-table order. */
void fs_list(void (*cb)(const char *name, int type, uint32_t length));

int fs_stat(const char *name, fs_stat_t *out);

/* Copies up to bufsize-1 bytes of file content into buf and NUL-terminates
 * it. Returns the number of bytes copied, or -1 if the file doesn't exist
 * or doesn't fit. */
int fs_read(const char *name, char *buf, uint32_t bufsize);

/* Writes (or overwrites) a file with the given NUL-terminated content.
 * Returns 0 on success, -1 on error (name too long, out of space/slots). */
int fs_write(const char *name, const char *content);

int fs_mkdir(const char *name);
int fs_remove(const char *name);
int fs_rename(const char *old_name, const char *new_name);

#endif
