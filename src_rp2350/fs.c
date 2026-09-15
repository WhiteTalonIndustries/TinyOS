#include <stdint.h>
#include <string.h>
#include "fs.h"
#include "ff.h"
#include "DEV_Config.h"

/* microSD (via FatFs + Waveshare's MMC_SD.c SPI driver) as TinyOS's user
 * storage -- replaces the earlier flash-backed fs.c (src_2040/fs.c's
 * TinyFS format) entirely, so the onboard 16MB flash stays reserved for
 * the system image itself, not user files. Same fs.h interface as before,
 * so main.c's shell commands needed zero changes. FatFs here is an older
 * elm-chan release (pre-FF_ prefix config names, f_mkfs(path,sfd,au)
 * 3-arg form) -- see lib/fatfs/00readme.txt. TCHAR is plain char (no LFN),
 * so filenames are 8.3 format (fits well within FS_NAME_LEN). */

static FATFS fatfs;
static uint8_t mounted;
static uint8_t exposed_to_usb;

void fs_init(void) {
    /* SD_CS is shared on SPI1 with the LCD (separate chip-selects) --
     * DEV_GPIO_Init() (called from System_Init() in main.c, before this)
     * already configured SD_CS_PIN as an output, deselected (high). */
    mounted = (f_mount(&fatfs, "0:", 1) == FR_OK);
}

void fs_format(void) {
    /* Real, destructive low-level format of the SD card -- not just
     * forgetting a directory table like the old flash-backed fs_format()
     * did. FF_USE_MKFS/_USE_MKFS must be 1 in ffconf.h (it is). au=0 lets
     * FatFs pick a sensible cluster size automatically. */
    if (f_mkfs("0:", 0, 0) == FR_OK) {
        f_mount(&fatfs, "0:", 1);
        mounted = 1;
    } else {
        mounted = 0;
    }
}

void fs_list(void (*cb)(const char *name, int type, uint32_t length)) {
    DIR dir;
    FILINFO info;

    if (!mounted) return;
    if (f_opendir(&dir, "0:/") != FR_OK) return;

    while (f_readdir(&dir, &info) == FR_OK && info.fname[0] != '\0') {
        int type = (info.fattrib & AM_DIR) ? FS_TYPE_DIR : FS_TYPE_FILE;
        cb(info.fname, type, (uint32_t)info.fsize);
    }
    f_closedir(&dir);
}

int fs_stat(const char *name, fs_stat_t *out) {
    FILINFO info;
    if (!mounted) return -1;
    if (f_stat(name, &info) != FR_OK) return -1;
    out->length = (uint32_t)info.fsize;
    return 0;
}

int fs_read(const char *name, char *buf, uint32_t bufsize) {
    FIL fp;
    UINT br;
    if (!mounted) return -1;
    if (f_open(&fp, name, FA_READ) != FR_OK) return -1;
    if (f_size(&fp) >= bufsize) { f_close(&fp); return -1; }
    if (f_read(&fp, buf, bufsize - 1, &br) != FR_OK) { f_close(&fp); return -1; }
    f_close(&fp);
    buf[br] = '\0';
    return (int)br;
}

int fs_write(const char *name, const char *content) {
    FIL fp;
    UINT bw;
    if (!mounted) return -1;
    if (name[0] == '\0' || strlen(name) >= FS_NAME_LEN) return -1;
    if (f_open(&fp, name, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return -1;
    if (f_write(&fp, content, (UINT)strlen(content), &bw) != FR_OK) { f_close(&fp); return -1; }
    f_close(&fp);
    return 0;
}

int fs_mkdir(const char *name) {
    if (!mounted) return -1;
    if (name[0] == '\0' || strlen(name) >= FS_NAME_LEN) return -1;
    return (f_mkdir(name) == FR_OK) ? 0 : -1;
}

int fs_remove(const char *name) {
    if (!mounted) return -1;
    return (f_unlink(name) == FR_OK) ? 0 : -1;
}

int fs_rename(const char *old_name, const char *new_name) {
    if (!mounted) return -1;
    if (new_name[0] == '\0' || strlen(new_name) >= FS_NAME_LEN) return -1;
    return (f_rename(old_name, new_name) == FR_OK) ? 0 : -1;
}

int fs_usb_mount(void) {
    if (exposed_to_usb) return 0;
    /* f_mount(NULL, ...) unmounts without touching media -- releases our
     * hold on the card so msc_disk.c's raw sector I/O doesn't race FatFs's
     * own in-RAM state (window/FAT caches, etc.) against the host. */
    f_mount((FATFS *)0, "0:", 0);
    mounted = 0;
    exposed_to_usb = 1;
    return 0;
}

int fs_usb_unmount(void) {
    if (!exposed_to_usb) return 0;
    exposed_to_usb = 0;
    mounted = (f_mount(&fatfs, "0:", 1) == FR_OK);
    return mounted ? 0 : -1;
}

int fs_usb_is_mounted(void) {
    return exposed_to_usb;
}
