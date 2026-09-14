#include <stdint.h>
#include "fs.h"
#include "flash.h"
#include "string.h"

/* Flat filesystem: sector 0 holds a superblock (magic + a fixed directory
 * table of FS_MAX_FILES entries). File data is appended sequentially into
 * the rest of flash starting at FS_DATA_START -- a bump allocator with no
 * reclamation. Deleting a file frees its directory slot but not its flash
 * space; `format` is the only way to reclaim data space. */

/* The first 256KB of flash is reserved for the kernel image itself (the
 * flash-boot build's code/rodata/data-copy live there -- see
 * linker_flash.ld). The RAM-boot build doesn't need the reservation, but
 * uses the same offset anyway rather than branching the filesystem layout
 * per build: keeps one on-flash format that works no matter which kernel
 * image is currently running. */
#define FS_BASE_OFFSET   0x40000u
#define FS_FLASH_SIZE    (8u * 1024u * 1024u - FS_BASE_OFFSET)
#define FS_DATA_START    (FS_BASE_OFFSET + FLASH_SECTOR_SIZE)
#define FS_MAX_FILE_SIZE FLASH_SECTOR_SIZE

typedef struct {
    uint8_t used;
    uint8_t type;
    char name[FS_NAME_LEN];
    uint32_t offset;
    uint32_t length;
} fs_entry_t;

typedef struct {
    char magic[4];
    uint32_t version;
    uint32_t next_free_offset;
    fs_entry_t entries[FS_MAX_FILES];
} fs_superblock_t;

static fs_superblock_t sb;
static uint8_t sector_buf[FLASH_SECTOR_SIZE];
static uint8_t write_buf[FS_MAX_FILE_SIZE];

static uint32_t round_up(uint32_t v, uint32_t align) {
    return (v + align - 1) / align * align;
}

static void copy_name(char *dst, const char *src) {
    int i;
    for (i = 0; i < FS_NAME_LEN - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static fs_entry_t *find_entry(const char *name) {
    int i;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (sb.entries[i].used && strcmp(sb.entries[i].name, name) == 0) return &sb.entries[i];
    }
    return NULL;
}

static fs_entry_t *find_free_entry(void) {
    int i;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (!sb.entries[i].used) return &sb.entries[i];
    }
    return NULL;
}

static void fs_write_superblock(void) {
    const uint8_t *src = (const uint8_t *)&sb;
    uint32_t i;
    for (i = 0; i < FLASH_SECTOR_SIZE; i++) sector_buf[i] = 0xff;
    for (i = 0; i < sizeof(sb); i++) sector_buf[i] = src[i];
    flash_erase(FS_BASE_OFFSET, FLASH_SECTOR_SIZE);
    flash_program(FS_BASE_OFFSET, sector_buf, FLASH_SECTOR_SIZE);
}

void fs_format(void) {
    int i;
    sb.magic[0] = 'T'; sb.magic[1] = 'F'; sb.magic[2] = 'S'; sb.magic[3] = '1';
    sb.version = 1;
    sb.next_free_offset = FS_DATA_START;
    for (i = 0; i < FS_MAX_FILES; i++) sb.entries[i].used = 0;
    fs_write_superblock();
}

void fs_init(void) {
    const fs_superblock_t *onflash = (const fs_superblock_t *)flash_read_ptr(FS_BASE_OFFSET);
    if (onflash->magic[0] == 'T' && onflash->magic[1] == 'F' &&
        onflash->magic[2] == 'S' && onflash->magic[3] == '1') {
        const uint8_t *src = (const uint8_t *)onflash;
        uint8_t *dst = (uint8_t *)&sb;
        uint32_t i;
        for (i = 0; i < sizeof(sb); i++) dst[i] = src[i];
    } else {
        fs_format();
    }
}

void fs_list(void (*cb)(const char *name, int type, uint32_t length)) {
    int i;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (sb.entries[i].used) cb(sb.entries[i].name, sb.entries[i].type, sb.entries[i].length);
    }
}

int fs_stat(const char *name, fs_stat_t *out) {
    fs_entry_t *e = find_entry(name);
    if (!e) return -1;
    out->length = e->length;
    return 0;
}

int fs_read(const char *name, char *buf, uint32_t bufsize) {
    fs_entry_t *e = find_entry(name);
    const uint8_t *src;
    uint32_t i;
    if (!e || e->type != FS_TYPE_FILE) return -1;
    if (e->length >= bufsize) return -1;
    src = flash_read_ptr(e->offset);
    for (i = 0; i < e->length; i++) buf[i] = (char)src[i];
    buf[e->length] = '\0';
    return (int)e->length;
}

int fs_write(const char *name, const char *content) {
    uint32_t len, start, padded, erase_len, i;
    fs_entry_t *e;

    if (name[0] == '\0' || strlen(name) >= FS_NAME_LEN) return -1;
    len = (uint32_t)strlen(content);
    if (len >= FS_MAX_FILE_SIZE) return -1;

    e = find_entry(name);
    if (e && e->type != FS_TYPE_FILE) return -1;
    if (!e) {
        e = find_free_entry();
        if (!e) return -1;
    }

    start = sb.next_free_offset;
    padded = round_up(len > 0 ? len : FLASH_PAGE_SIZE, FLASH_PAGE_SIZE);
    erase_len = round_up(padded, FLASH_SECTOR_SIZE);
    if (start + erase_len > FS_FLASH_SIZE) return -1;

    for (i = 0; i < padded; i++) write_buf[i] = 0xff;
    for (i = 0; i < len; i++) write_buf[i] = (uint8_t)content[i];

    flash_erase(start, erase_len);
    flash_program(start, write_buf, padded);

    copy_name(e->name, name);
    e->type = FS_TYPE_FILE;
    e->offset = start;
    e->length = len;
    e->used = 1;
    sb.next_free_offset = start + erase_len;

    fs_write_superblock();
    return 0;
}

int fs_mkdir(const char *name) {
    fs_entry_t *e;
    if (name[0] == '\0' || strlen(name) >= FS_NAME_LEN) return -1;
    if (find_entry(name)) return -1;
    e = find_free_entry();
    if (!e) return -1;
    copy_name(e->name, name);
    e->type = FS_TYPE_DIR;
    e->offset = 0;
    e->length = 0;
    e->used = 1;
    fs_write_superblock();
    return 0;
}

int fs_remove(const char *name) {
    fs_entry_t *e = find_entry(name);
    if (!e) return -1;
    e->used = 0;
    fs_write_superblock();
    return 0;
}

int fs_rename(const char *old_name, const char *new_name) {
    fs_entry_t *e = find_entry(old_name);
    fs_entry_t *existing;
    if (!e) return -1;
    if (new_name[0] == '\0' || strlen(new_name) >= FS_NAME_LEN) return -1;
    existing = find_entry(new_name);
    if (existing && existing != e) existing->used = 0;
    copy_name(e->name, new_name);
    fs_write_superblock();
    return 0;
}
