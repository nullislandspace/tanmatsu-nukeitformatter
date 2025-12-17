#include "sdcard.h"
#include <string.h>
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sdcard";
static const char *MOUNT_POINT = "/sd";

static sdcard_status_t status = SDCARD_STATUS_NOT_INITIALIZED;
static sdmmc_card_t *card = NULL;
static sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;

// Host and slot config stored for remounting
static sdmmc_host_t host;
static sdmmc_slot_config_t slot_config;

esp_err_t sdcard_init(void) {
    esp_err_t ret;

    // Initialize LDO power control for SD card
    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = 4,
    };
    ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LDO power control: %s", esp_err_to_name(ret));
        status = SDCARD_STATUS_ERROR;
        return ret;
    }

    // Configure SDMMC host
    host = (sdmmc_host_t)SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;  // 40MHz
    host.pwr_ctrl_handle = pwr_ctrl_handle;

    // Configure slot with Tanmatsu pin configuration
    slot_config = (sdmmc_slot_config_t)SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.clk = GPIO_NUM_43;
    slot_config.cmd = GPIO_NUM_44;
    slot_config.d0 = GPIO_NUM_39;
    slot_config.d1 = GPIO_NUM_40;
    slot_config.d2 = GPIO_NUM_41;
    slot_config.d3 = GPIO_NUM_42;
    slot_config.width = 4;  // 4-bit mode
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    // Try to mount without auto-format first
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    ESP_LOGI(TAG, "Initializing SD card...");
    ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SD card mounted successfully");
        sdmmc_card_print_info(stdout, card);
        status = SDCARD_STATUS_MOUNTED;
        return ESP_OK;
    }

    if (ret == ESP_FAIL) {
        // Card present but filesystem mount failed (incompatible filesystem)
        ESP_LOGW(TAG, "Card present but filesystem mount failed - may need formatting");
        status = SDCARD_STATUS_MOUNT_FAILED;
        return ESP_FAIL;
    }

    if (ret == ESP_ERR_TIMEOUT || ret == ESP_ERR_NOT_FOUND) {
        ESP_LOGE(TAG, "No SD card detected");
        status = SDCARD_STATUS_NO_CARD;
        return ret;
    }

    ESP_LOGE(TAG, "SD card init error: %s", esp_err_to_name(ret));
    status = SDCARD_STATUS_ERROR;
    return ret;
}

sdcard_status_t sdcard_get_status(void) {
    return status;
}

esp_err_t sdcard_format(void) {
    esp_err_t ret;

    if (status == SDCARD_STATUS_MOUNTED) {
        // Card is mounted, use high-level format API
        ESP_LOGI(TAG, "Formatting mounted SD card...");
        ret = esp_vfs_fat_sdcard_format(MOUNT_POINT, card);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Format successful");
        } else {
            ESP_LOGE(TAG, "Format failed: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    if (status == SDCARD_STATUS_MOUNT_FAILED) {
        // Card has incompatible filesystem, mount with auto-format enabled
        // This will partition and format the card automatically
        ESP_LOGI(TAG, "Formatting card with incompatible filesystem...");

        esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = true,  // This triggers f_fdisk() + f_mkfs()
            .max_files = 5,
            .allocation_unit_size = 16 * 1024,
        };

        ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Format and mount successful");
            sdmmc_card_print_info(stdout, card);
            status = SDCARD_STATUS_MOUNTED;
            return ESP_OK;
        }

        ESP_LOGE(TAG, "Format failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGE(TAG, "Cannot format: no card or error state");
    return ESP_ERR_INVALID_STATE;
}

esp_err_t sdcard_wipe_sectors(size_t start_sector, size_t count) {
    esp_err_t ret;

    // Need to unmount first to write raw sectors safely
    if (status == SDCARD_STATUS_MOUNTED) {
        ESP_LOGI(TAG, "Unmounting card for raw access...");
        ret = esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to unmount: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    // If card was never initialized, we need to init the SDMMC host directly
    if (card == NULL) {
        ESP_LOGE(TAG, "Card not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Create zero buffer (aligned for DMA)
    static uint8_t zero_buffer[512] __attribute__((aligned(4)));
    memset(zero_buffer, 0, sizeof(zero_buffer));

    ESP_LOGI(TAG, "Wiping %zu sectors starting at sector %zu", count, start_sector);

    for (size_t i = 0; i < count; i++) {
        ret = sdmmc_write_sectors(card, zero_buffer, start_sector + i, 1);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to wipe sector %zu: %s", start_sector + i, esp_err_to_name(ret));
            status = SDCARD_STATUS_ERROR;
            return ret;
        }
    }

    ESP_LOGI(TAG, "Wipe complete - %zu sectors zeroed", count);
    status = SDCARD_STATUS_MOUNT_FAILED;  // Card no longer has valid filesystem
    return ESP_OK;
}

void sdcard_deinit(void) {
    if (status == SDCARD_STATUS_MOUNTED && card != NULL) {
        esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
    }
    card = NULL;
    status = SDCARD_STATUS_NOT_INITIALIZED;
}
