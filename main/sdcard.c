#include "sdcard.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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

    ESP_LOGI(TAG, "=== Initializing SD card subsystem ===");

    // Initialize LDO power control for SD card
    ESP_LOGI(TAG, "Setting up LDO power control (channel 4)...");
    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = 4,
    };
    ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LDO power control: %s", esp_err_to_name(ret));
        status = SDCARD_STATUS_ERROR;
        return ret;
    }

    // Power cycle the SD card to ensure it's in a known state
    // This prevents issues when the card was left in SDMMC mode from a previous session
    ESP_LOGI(TAG, "Power cycling SD card...");
    sd_pwr_ctrl_set_io_voltage(pwr_ctrl_handle, 0);      // Power off
    vTaskDelay(pdMS_TO_TICKS(150));                      // Wait 150ms
    sd_pwr_ctrl_set_io_voltage(pwr_ctrl_handle, 3300);   // Power on at 3.3V
    vTaskDelay(pdMS_TO_TICKS(150));                      // Wait 150ms for card to stabilize
    ESP_LOGI(TAG, "SD card power cycle complete");

    // Configure SDMMC host
    ESP_LOGI(TAG, "Configuring SDMMC host (slot 0, 40MHz)...");
    host = (sdmmc_host_t)SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;  // 40MHz
    host.pwr_ctrl_handle = pwr_ctrl_handle;

    // Allocate DMA buffer in internal RAM to avoid PSRAM cache sync overhead
    static DRAM_DMA_ALIGNED_ATTR uint8_t dma_buf[512 * 4];  // 2KB aligned buffer
    host.dma_aligned_buffer = dma_buf;

    // Configure slot with Tanmatsu pin configuration
    ESP_LOGI(TAG, "Configuring slot pins (CLK=43, CMD=44, D0-D3=39-42, 4-bit)...");
    slot_config = (sdmmc_slot_config_t)SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.clk = GPIO_NUM_43;
    slot_config.cmd = GPIO_NUM_44;
    slot_config.d0 = GPIO_NUM_39;
    slot_config.d1 = GPIO_NUM_40;
    slot_config.d2 = GPIO_NUM_41;
    slot_config.d3 = GPIO_NUM_42;
    slot_config.width = 4;  // 4-bit mode

    // Try to mount without auto-format first
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    ESP_LOGI(TAG, "Attempting to mount SD card at %s...", MOUNT_POINT);
    ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    ESP_LOGI(TAG, "Mount result: %s (0x%x)", esp_err_to_name(ret), ret);

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

    ESP_LOGI(TAG, "=== Starting format operation ===");
    ESP_LOGI(TAG, "Current status: %d, card ptr: %p", status, (void*)card);

    if (status == SDCARD_STATUS_MOUNTED) {
        // Card is mounted, use high-level format API
        ESP_LOGI(TAG, "Card is mounted, using esp_vfs_fat_sdcard_format()...");
        ret = esp_vfs_fat_sdcard_format(MOUNT_POINT, card);
        ESP_LOGI(TAG, "Format result: %s (0x%x)", esp_err_to_name(ret), ret);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "=== Format successful ===");
        } else {
            ESP_LOGE(TAG, "=== Format failed ===");
        }
        return ret;
    }

    if (status == SDCARD_STATUS_MOUNT_FAILED) {
        // Card has incompatible filesystem, mount with auto-format enabled
        // This will partition and format the card automatically
        ESP_LOGI(TAG, "Card has incompatible filesystem, using format_if_mount_failed...");

        esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = true,  // This triggers f_fdisk() + f_mkfs()
            .max_files = 5,
            .allocation_unit_size = 16 * 1024,
        };

        ESP_LOGI(TAG, "Mounting with auto-format enabled...");
        ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
        ESP_LOGI(TAG, "Mount/format result: %s (0x%x)", esp_err_to_name(ret), ret);

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "=== Format and mount successful ===");
            sdmmc_card_print_info(stdout, card);
            status = SDCARD_STATUS_MOUNTED;
            return ESP_OK;
        }

        ESP_LOGE(TAG, "=== Format failed ===");
        return ret;
    }

    ESP_LOGE(TAG, "Cannot format: invalid state (status=%d)", status);
    return ESP_ERR_INVALID_STATE;
}

esp_err_t sdcard_wipe_sectors(size_t start_sector, size_t count) {
    esp_err_t ret;

    ESP_LOGI(TAG, "=== Starting sector wipe ===");
    ESP_LOGI(TAG, "Current status: %d, card ptr: %p", status, (void*)card);

    // Need to unmount first to write raw sectors safely
    if (status == SDCARD_STATUS_MOUNTED) {
        ESP_LOGI(TAG, "Unmounting card for raw access...");
        ret = esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to unmount: %s", esp_err_to_name(ret));
            return ret;
        }
        ESP_LOGI(TAG, "Unmount successful, card ptr now: %p", (void*)card);
        // Note: unmount deinitializes the SDMMC driver, card handle is now invalid
        card = NULL;
    }

    // Reinitialize SDMMC host and card for raw access (without mounting filesystem)
    ESP_LOGI(TAG, "Reinitializing SDMMC for raw access...");

    // Allocate card structure
    static sdmmc_card_t raw_card;
    memset(&raw_card, 0, sizeof(raw_card));

    // Initialize SDMMC host
    ESP_LOGI(TAG, "Initializing SDMMC host...");
    ret = sdmmc_host_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init SDMMC host: %s", esp_err_to_name(ret));
        return ret;
    }

    // Initialize slot
    ESP_LOGI(TAG, "Initializing SDMMC slot...");
    ret = sdmmc_host_init_slot(host.slot, &slot_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init SDMMC slot: %s", esp_err_to_name(ret));
        sdmmc_host_deinit();
        return ret;
    }

    // Initialize card
    ESP_LOGI(TAG, "Probing SD card...");
    ret = sdmmc_card_init(&host, &raw_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init card: %s", esp_err_to_name(ret));
        sdmmc_host_deinit();
        return ret;
    }

    ESP_LOGI(TAG, "Card initialized successfully");
    ESP_LOGI(TAG, "Card sector size: %d bytes", (int)raw_card.csd.sector_size);
    sdmmc_card_print_info(stdout, &raw_card);

    // Create zero buffer (aligned for DMA)
    // Use larger buffer for efficiency - write multiple sectors at once
    #define WIPE_BUFFER_SECTORS 16
    static uint8_t zero_buffer[512 * WIPE_BUFFER_SECTORS] __attribute__((aligned(4)));
    memset(zero_buffer, 0, sizeof(zero_buffer));

    ESP_LOGI(TAG, "Wiping %zu sectors starting at sector %zu", count, start_sector);
    ESP_LOGI(TAG, "Using %d-sector buffer for writes", WIPE_BUFFER_SECTORS);

    size_t sectors_remaining = count;
    size_t current_sector = start_sector;

    while (sectors_remaining > 0) {
        size_t sectors_to_write = (sectors_remaining > WIPE_BUFFER_SECTORS)
                                  ? WIPE_BUFFER_SECTORS : sectors_remaining;

        ESP_LOGI(TAG, "Writing %zu sectors at sector %zu (%zu remaining)",
                 sectors_to_write, current_sector, sectors_remaining);

        ret = sdmmc_write_sectors(&raw_card, zero_buffer, current_sector, sectors_to_write);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to wipe sectors at %zu: %s", current_sector, esp_err_to_name(ret));
            sdmmc_host_deinit();
            status = SDCARD_STATUS_ERROR;
            return ret;
        }

        current_sector += sectors_to_write;
        sectors_remaining -= sectors_to_write;
    }

    ESP_LOGI(TAG, "=== Wipe complete - %zu sectors zeroed ===", count);

    // Deinitialize SDMMC host
    sdmmc_host_deinit();

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
