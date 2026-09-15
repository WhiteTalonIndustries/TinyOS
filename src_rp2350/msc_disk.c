/* USB Mass Storage backend for the "mount"/"unmount" shell commands --
 * exposes the same physical SD card TinyOS's own FatFs uses (fs.c) as a
 * raw block device to whatever PC the USB cable is plugged into. Reads
 * and writes go straight to MMC_SD.c's sector I/O (the same calls
 * lib/fatfs/diskio.c makes for TinyOS's own filesystem), bypassing FatFs
 * entirely -- from the host's point of view this is a normal USB flash
 * drive with an actual FAT filesystem on it already, since MMC_SD.c reads
 * the same physical medium fs.c formats and writes.
 *
 * Card is exclusive-access: fs_usb_mount()/fs_usb_unmount() (fs.c) gate
 * host access via sd_exposed_to_usb, unmounting/remounting TinyOS's own
 * FatFs at the same time so exactly one side ever touches the card. */

#include <string.h>
#include "tusb.h"
#include "MMC_SD.h"
#include "fs.h"

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]) {
    (void)lun;
    const char vid[] = "TinyOS";
    const char pid[] = "SD Card";
    const char rev[] = "1.0";
    memcpy(vendor_id, vid, strlen(vid));
    memcpy(product_id, pid, strlen(pid));
    memcpy(product_rev, rev, strlen(rev));
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    (void)lun;
    if (!fs_usb_is_mounted()) {
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0x00);
        return false;
    }
    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
    (void)lun;
    *block_count = fs_usb_is_mounted() ? SD_GetSectorCount() : 0;
    *block_size = 512;
}

bool tud_msc_is_writable_cb(uint8_t lun) {
    (void)lun;
    return true;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
    (void)lun;
    (void)power_condition;
    (void)start;
    /* Host "safely ejected" the drive -- hand the card back to TinyOS
     * without requiring an explicit `unmount` at the console. */
    if (load_eject && !start) {
        fs_usb_unmount();
    }
    return true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
    (void)lun;
    if (!fs_usb_is_mounted() || offset != 0 || bufsize != 512) return -1;
    if (SD_ReadDisk((uint8_t *)buffer, lba, 1) != 0) return -1;
    return 512;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
    (void)lun;
    if (!fs_usb_is_mounted() || offset != 0 || bufsize != 512) return -1;
    if (SD_WriteDisk(buffer, lba, 1) != 0) return -1;
    return 512;
}

/* Any SCSI command not covered by the callbacks above (we only need plain
 * block reads/writes) -- fail it rather than silently no-op. */
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize) {
    (void)lun;
    (void)scsi_cmd;
    (void)buffer;
    (void)bufsize;
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
    return -1;
}
