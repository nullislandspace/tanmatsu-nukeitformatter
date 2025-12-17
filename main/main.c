#include <stdio.h>
#include "bsp/device.h"
#include "bsp/display.h"
#include "bsp/input.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_types.h"
#include "esp_log.h"
#include "hal/lcd_types.h"
#include "nvs_flash.h"
#include "pax_fonts.h"
#include "pax_gfx.h"
#include "pax_text.h"
#include "portmacro.h"

#include "sdcard.h"
#include "ui.h"

// Constants
static char const TAG[] = "main";

// Application states
typedef enum {
    STATE_INIT_ERROR,       // SD card init failed
    STATE_MAIN_MENU,        // Main menu
    STATE_CONFIRM_FORMAT,   // Confirm format dialog
    STATE_CONFIRM_EXIT,     // Confirm exit dialog
    STATE_CONFIRM_WIPE,     // Hidden: confirm wipe dialog
    STATE_FORMATTING,       // Formatting in progress
    STATE_WIPING,           // Wiping in progress
    STATE_SUCCESS,          // Format succeeded - will exit to launcher
    STATE_ERROR,            // Operation failed
    STATE_WIPE_DONE,        // Wipe complete - will exit to launcher
} app_state_t;

// Global variables
static size_t display_h_res = 0;
static size_t display_v_res = 0;
static lcd_color_rgb_pixel_format_t display_color_format = LCD_COLOR_PIXEL_FORMAT_RGB565;
static lcd_rgb_data_endian_t display_data_endian = LCD_RGB_DATA_ENDIAN_LITTLE;
static pax_buf_t fb = {0};
static QueueHandle_t input_event_queue = NULL;

static app_state_t app_state = STATE_MAIN_MENU;
static int menu_selection = 0;
static char error_message[128] = "";

void app_main(void) {
    // Start the GPIO interrupt service
    gpio_install_isr_service(0);

    // Initialize the Non Volatile Storage service
    esp_err_t res = nvs_flash_init();
    if (res == ESP_ERR_NVS_NO_FREE_PAGES || res == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        res = nvs_flash_init();
    }
    ESP_ERROR_CHECK(res);

    // Initialize the Board Support Package
    const bsp_configuration_t bsp_configuration = {
        .display =
            {
                .requested_color_format = display_color_format,
                .num_fbs = 1,
            },
    };
    ESP_ERROR_CHECK(bsp_device_initialize(&bsp_configuration));

    // Get display parameters and rotation
    res = bsp_display_get_parameters(&display_h_res, &display_v_res, &display_color_format, &display_data_endian);
    ESP_ERROR_CHECK(res);
    bsp_display_rotation_t display_rotation = bsp_display_get_default_rotation();

    // Convert ESP-IDF color format into PAX buffer type
    pax_buf_type_t format = PAX_BUF_24_888RGB;
    switch (display_color_format) {
        case LCD_COLOR_PIXEL_FORMAT_RGB565:
            format = PAX_BUF_16_565RGB;
            break;
        case LCD_COLOR_PIXEL_FORMAT_RGB888:
            format = PAX_BUF_24_888RGB;
            break;
        default:
            break;
    }

    // Convert BSP display rotation format into PAX orientation type
    pax_orientation_t orientation = PAX_O_UPRIGHT;
    switch (display_rotation) {
        case BSP_DISPLAY_ROTATION_90:
            orientation = PAX_O_ROT_CCW;
            break;
        case BSP_DISPLAY_ROTATION_180:
            orientation = PAX_O_ROT_HALF;
            break;
        case BSP_DISPLAY_ROTATION_270:
            orientation = PAX_O_ROT_CW;
            break;
        case BSP_DISPLAY_ROTATION_0:
        default:
            orientation = PAX_O_UPRIGHT;
            break;
    }

    // Initialize graphics stack
    pax_buf_init(&fb, NULL, display_h_res, display_v_res, format);
    pax_buf_reversed(&fb, display_data_endian == LCD_RGB_DATA_ENDIAN_BIG);
    pax_buf_set_orientation(&fb, orientation);

    // Initialize UI module
    ui_init(&fb);

    // Get input event queue from BSP
    ESP_ERROR_CHECK(bsp_input_get_queue(&input_event_queue));

    // Show initializing message
    ui_draw_wait("Initializing SD card...");
    ui_blit();

    // Initialize SD card
    res = sdcard_init();
    sdcard_status_t sd_status = sdcard_get_status();

    if (sd_status == SDCARD_STATUS_NO_CARD || sd_status == SDCARD_STATUS_ERROR) {
        // Fatal error - no card or init failed completely
        snprintf(error_message, sizeof(error_message), "Error: %s",
                 sd_status == SDCARD_STATUS_NO_CARD ? "No SD card detected" : "SD card initialization failed");
        app_state = STATE_INIT_ERROR;
        ui_draw_error("SD Card Error", error_message);
        ui_blit();
    } else {
        // Card present (mounted or mount failed due to incompatible FS)
        app_state = STATE_MAIN_MENU;
        menu_selection = 0;
        ui_draw_main_menu(menu_selection);
        ui_blit();
    }

    // Main event loop
    while (1) {
        bsp_input_event_t event;
        if (xQueueReceive(input_event_queue, &event, portMAX_DELAY) == pdTRUE) {
            bool redraw = false;

            // Handle navigation events (function keys, arrows, etc.)
            if (event.type == INPUT_EVENT_TYPE_NAVIGATION && event.args_navigation.state) {
                switch (app_state) {
                    case STATE_INIT_ERROR:
                        // Any key exits to launcher
                        bsp_device_restart_to_launcher();
                        break;

                    case STATE_MAIN_MENU:
                        switch (event.args_navigation.key) {
                            case BSP_INPUT_NAVIGATION_KEY_UP:
                                if (menu_selection > 0) {
                                    menu_selection--;
                                    redraw = true;
                                }
                                break;

                            case BSP_INPUT_NAVIGATION_KEY_DOWN:
                                if (menu_selection < 2) {
                                    menu_selection++;
                                    redraw = true;
                                }
                                break;

                            case BSP_INPUT_NAVIGATION_KEY_RETURN:
                                switch (menu_selection) {
                                    case 0:
                                        app_state = STATE_CONFIRM_FORMAT;
                                        break;
                                    case 1:
                                        app_state = STATE_CONFIRM_WIPE;
                                        break;
                                    case 2:
                                        app_state = STATE_CONFIRM_EXIT;
                                        break;
                                }
                                redraw = true;
                                break;

                            case BSP_INPUT_NAVIGATION_KEY_ESC:
                                app_state = STATE_CONFIRM_EXIT;
                                redraw = true;
                                break;

                            case BSP_INPUT_NAVIGATION_KEY_F1:
                                app_state = STATE_CONFIRM_FORMAT;
                                redraw = true;
                                break;

                            case BSP_INPUT_NAVIGATION_KEY_F3:
                                app_state = STATE_CONFIRM_WIPE;
                                redraw = true;
                                break;

                            default:
                                break;
                        }
                        break;

                    case STATE_CONFIRM_FORMAT:
                    case STATE_CONFIRM_EXIT:
                    case STATE_CONFIRM_WIPE:
                        if (event.args_navigation.key == BSP_INPUT_NAVIGATION_KEY_ESC) {
                            app_state = STATE_MAIN_MENU;
                            redraw = true;
                        }
                        break;

                    case STATE_SUCCESS:
                    case STATE_WIPE_DONE:
                        // Any key exits to launcher
                        bsp_device_restart_to_launcher();
                        break;

                    case STATE_ERROR:
                        // Any key returns to main menu
                        app_state = STATE_MAIN_MENU;
                        redraw = true;
                        break;

                    default:
                        break;
                }
            }

            // Handle keyboard events (Y/N for confirmation)
            if (event.type == INPUT_EVENT_TYPE_KEYBOARD) {
                char key = event.args_keyboard.ascii;

                switch (app_state) {
                    case STATE_INIT_ERROR:
                        // Any key exits to launcher
                        bsp_device_restart_to_launcher();
                        break;

                    case STATE_CONFIRM_FORMAT:
                        if (key == 'y' || key == 'Y') {
                            app_state = STATE_FORMATTING;
                            ui_draw_wait("Formatting SD card...");
                            ui_blit();

                            // Perform format
                            res = sdcard_format();
                            if (res == ESP_OK) {
                                app_state = STATE_SUCCESS;
                                ui_draw_success("SD card formatted successfully!\nPress any key to exit.");
                            } else {
                                snprintf(error_message, sizeof(error_message),
                                         "Format failed: %s", esp_err_to_name(res));
                                app_state = STATE_ERROR;
                                ui_draw_error("Format Failed", error_message);
                            }
                            ui_blit();
                        } else if (key == 'n' || key == 'N') {
                            app_state = STATE_MAIN_MENU;
                            redraw = true;
                        }
                        break;

                    case STATE_CONFIRM_EXIT:
                        if (key == 'y' || key == 'Y') {
                            bsp_device_restart_to_launcher();
                        } else if (key == 'n' || key == 'N') {
                            app_state = STATE_MAIN_MENU;
                            redraw = true;
                        }
                        break;

                    case STATE_CONFIRM_WIPE:
                        if (key == 'y' || key == 'Y') {
                            app_state = STATE_WIPING;
                            ui_draw_wait("Wiping partition table and boot sectors...");
                            ui_blit();

                            // Wipe first 128 sectors (64KB) to ensure partition table,
                            // MBR, and FAT boot sector are all destroyed
                            res = sdcard_wipe_sectors(0, 128);
                            if (res == ESP_OK) {
                                ESP_LOGI(TAG, "Wipe complete");
                                ui_draw_success("SD card wiped successfully.\nCard will appear unformatted.\nPress any key to exit.");
                            } else {
                                ESP_LOGE(TAG, "Wipe failed: %s", esp_err_to_name(res));
                                snprintf(error_message, sizeof(error_message),
                                         "Wipe failed: %s", esp_err_to_name(res));
                                ui_draw_error("Wipe Failed", error_message);
                            }
                            ui_blit();
                            // Wait for key press then exit
                            app_state = STATE_WIPE_DONE;
                        } else if (key == 'n' || key == 'N') {
                            app_state = STATE_MAIN_MENU;
                            redraw = true;
                        }
                        break;

                    case STATE_WIPE_DONE:
                    case STATE_SUCCESS:
                        // Any key exits to launcher
                        bsp_device_restart_to_launcher();
                        break;

                    case STATE_ERROR:
                        // Any key returns to main menu
                        app_state = STATE_MAIN_MENU;
                        redraw = true;
                        break;

                    default:
                        break;
                }
            }

            // Redraw UI if needed
            if (redraw) {
                switch (app_state) {
                    case STATE_MAIN_MENU:
                        ui_draw_main_menu(menu_selection);
                        break;

                    case STATE_CONFIRM_FORMAT:
                        ui_draw_confirm("Format SD Card?",
                                        "All data will be lost!");
                        break;

                    case STATE_CONFIRM_EXIT:
                        ui_draw_confirm("Exit to Launcher?",
                                        "Return to the main launcher?");
                        break;

                    case STATE_CONFIRM_WIPE:
                        ui_draw_confirm("Wipe SD Card?",
                                        "This will erase partition table and filesystem!");
                        break;

                    default:
                        break;
                }
                ui_blit();
            }
        }
    }
}
