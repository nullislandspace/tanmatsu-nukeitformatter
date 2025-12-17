#include "ui.h"
#include "bsp/display.h"
#include "pax_fonts.h"

static pax_buf_t *fb = NULL;
static size_t display_w = 0;
static size_t display_h = 0;

#define COLOR_BG      0xFFFFFFFF
#define COLOR_TEXT    0xFF000000
#define COLOR_SELECT  0xFF0066CC
#define COLOR_ERROR   0xFFCC0000
#define COLOR_SUCCESS 0xFF00AA00

void ui_init(pax_buf_t *framebuffer) {
    fb = framebuffer;
    display_w = pax_buf_get_width(fb);
    display_h = pax_buf_get_height(fb);
}

void ui_blit(void) {
    bsp_display_blit(0, 0, display_w, display_h, pax_buf_get_pixels(fb));
}

void ui_draw_main_menu(int selected) {
    pax_background(fb, COLOR_BG);

    // Title
    pax_draw_text(fb, COLOR_TEXT, pax_font_sky_mono, 24, 10, 10, "SD Card Formatter");

    // Separator line
    pax_simple_rect(fb, COLOR_TEXT, 10, 45, display_w - 20, 2);

    // Menu items
    const char *items[] = {"Format SD card", "Exit"};
    int y = 70;

    for (int i = 0; i < 2; i++) {
        pax_col_t color = (i == selected) ? COLOR_SELECT : COLOR_TEXT;
        char line[64];
        snprintf(line, sizeof(line), "%s %s", (i == selected) ? ">" : " ", items[i]);
        pax_draw_text(fb, color, pax_font_sky_mono, 18, 20, y, line);
        y += 30;
    }

    // Instructions at bottom
    pax_draw_text(fb, COLOR_TEXT, pax_font_sky_mono, 14, 10, display_h - 65,
                  "Up/Down: Select");
    pax_draw_text(fb, COLOR_TEXT, pax_font_sky_mono, 14, 10, display_h - 45,
                  "Enter/F1: Format SD card");
    pax_draw_text(fb, COLOR_TEXT, pax_font_sky_mono, 14, 10, display_h - 25,
                  "Escape: Exit to launcher");
}

void ui_draw_confirm(const char *title, const char *message) {
    pax_background(fb, COLOR_BG);

    // Title
    pax_draw_text(fb, COLOR_TEXT, pax_font_sky_mono, 20, 10, 30, title);

    // Separator
    pax_simple_rect(fb, COLOR_TEXT, 10, 60, display_w - 20, 2);

    // Message
    pax_draw_text(fb, COLOR_TEXT, pax_font_sky_mono, 16, 10, 90, message);

    // Instructions
    pax_draw_text(fb, COLOR_SUCCESS, pax_font_sky_mono, 16, 10, 150, "Press Y to confirm");
    pax_draw_text(fb, COLOR_ERROR, pax_font_sky_mono, 16, 10, 175, "Press N or Escape to cancel");
}

void ui_draw_wait(const char *message) {
    pax_background(fb, COLOR_BG);

    // Center the "Please wait..." text
    int center_y = display_h / 2 - 30;
    pax_draw_text(fb, COLOR_TEXT, pax_font_sky_mono, 24, 10, center_y, "Please wait...");

    // Message below
    pax_draw_text(fb, COLOR_TEXT, pax_font_sky_mono, 16, 10, center_y + 40, message);
}

void ui_draw_error(const char *title, const char *message) {
    pax_background(fb, COLOR_BG);

    // Error title in red
    pax_draw_text(fb, COLOR_ERROR, pax_font_sky_mono, 24, 10, 30, title);

    // Separator
    pax_simple_rect(fb, COLOR_ERROR, 10, 65, display_w - 20, 2);

    // Message
    pax_draw_text(fb, COLOR_TEXT, pax_font_sky_mono, 16, 10, 95, message);

    // Instructions
    pax_draw_text(fb, COLOR_TEXT, pax_font_sky_mono, 14, 10, display_h - 30,
                  "Press any key to continue");
}

void ui_draw_success(const char *message) {
    pax_background(fb, COLOR_BG);

    // Success title in green
    pax_draw_text(fb, COLOR_SUCCESS, pax_font_sky_mono, 24, 10, 30, "Success!");

    // Separator
    pax_simple_rect(fb, COLOR_SUCCESS, 10, 65, display_w - 20, 2);

    // Message
    pax_draw_text(fb, COLOR_TEXT, pax_font_sky_mono, 16, 10, 95, message);

    // Instructions
    pax_draw_text(fb, COLOR_TEXT, pax_font_sky_mono, 14, 10, display_h - 30,
                  "Press any key to continue");
}
