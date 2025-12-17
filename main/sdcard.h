#pragma once

#include "esp_err.h"
#include "sdmmc_cmd.h"

typedef enum {
    SDCARD_STATUS_NOT_INITIALIZED,
    SDCARD_STATUS_NO_CARD,        // No card detected
    SDCARD_STATUS_MOUNTED,        // Mounted and ready
    SDCARD_STATUS_MOUNT_FAILED,   // Card present, incompatible filesystem
    SDCARD_STATUS_ERROR,          // Other error
} sdcard_status_t;

// Initialize SD card subsystem and try to mount
// Returns ESP_OK if mounted, ESP_FAIL if card present but mount failed
esp_err_t sdcard_init(void);

// Get current status
sdcard_status_t sdcard_get_status(void);

// Format the SD card
// - If mounted: uses esp_vfs_fat_sdcard_format()
// - If mount failed: remounts with format_if_mount_failed = true
esp_err_t sdcard_format(void);

// Wipe first N sectors with zeros (for hidden wipe function)
// Note: This will unmount the card and leave it in unmounted state
esp_err_t sdcard_wipe_sectors(size_t start_sector, size_t count);

// Unmount and deinitialize
void sdcard_deinit(void);
