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
    // Get logical dimensions for UI drawing
    display_w = pax_buf_get_width(fb);
    display_h = pax_buf_get_height(fb);
}

void ui_blit(void) {
    // Swap dimensions for blit due to display rotation
    bsp_display_blit(0, 0, display_h, display_w, pax_buf_get_pixels(fb));
}

// Draw a symbolic SD card icon
static void draw_sd_card_icon(int x, int y, int w, int h) {
    int notch = w / 5;  // Cut corner size

    // Colors
    pax_col_t card_fill = 0xFFDDDDDD;     // Light gray fill
    pax_col_t card_outline = 0xFF444444;  // Dark gray outline
    pax_col_t contact_color = 0xFFCCAA00; // Gold contacts

    // Draw card body (two rectangles to create notch shape)
    pax_simple_rect(fb, card_fill, x, y + notch, w, h - notch);
    pax_simple_rect(fb, card_fill, x, y, w - notch, notch);

    // Draw outline using thin rectangles (1px lines)
    // Left edge
    pax_simple_rect(fb, card_outline, x, y, 2, h);
    // Bottom edge
    pax_simple_rect(fb, card_outline, x, y + h - 2, w, 2);
    // Right edge
    pax_simple_rect(fb, card_outline, x + w - 2, y + notch, 2, h - notch);
    // Top edge (before notch)
    pax_simple_rect(fb, card_outline, x, y, w - notch, 2);
    // Notch diagonal (approximate with small rectangles)
    for (int i = 0; i < notch; i++) {
        pax_simple_rect(fb, card_outline, x + w - notch + i, y + i, 2, 2);
    }

    // Draw contact strips (gold rectangles)
    int contact_w = w / 10;
    int contact_h = h / 5;
    int contact_gap = contact_w + 2;
    int num_contacts = 6;
    int contacts_total_w = num_contacts * contact_w + (num_contacts - 1) * 2;
    int contacts_x = x + (w - contacts_total_w) / 2;
    int contacts_y = y + notch + 10;

    for (int i = 0; i < num_contacts; i++) {
        pax_simple_rect(fb, contact_color, contacts_x + i * contact_gap, contacts_y, contact_w, contact_h);
    }

    // Draw label area (lighter rectangle in middle)
    int label_h = h / 4;
    int label_y = y + h - label_h - 15;
    pax_simple_rect(fb, 0xFFEEEEEE, x + 10, label_y, w - 20, label_h);
    pax_simple_rect(fb, card_outline, x + 10, label_y, w - 20, 2);
    pax_simple_rect(fb, card_outline, x + 10, label_y + label_h - 2, w - 20, 2);
}

void ui_draw_main_menu(int selected) {
    pax_background(fb, COLOR_BG);

    // Title
    pax_draw_text(fb, COLOR_TEXT, pax_font_sky_mono, 24, 10, 10, "Nuke-it SD Card Format");

    // Separator line
    pax_simple_rect(fb, COLOR_TEXT, 10, 45, display_w - 20, 2);

    // Menu items
    const char *items[] = {"Format SD card", "Wipe partition table", "Exit"};
    int y = 70;

    for (int i = 0; i < 3; i++) {
        pax_col_t color = (i == selected) ? COLOR_SELECT : COLOR_TEXT;
        char line[64];
        snprintf(line, sizeof(line), "%s %s", (i == selected) ? ">" : " ", items[i]);
        pax_draw_text(fb, color, pax_font_sky_mono, 18, 20, y, line);
        y += 30;
    }

    // Draw SD card icon on right side
    int card_w = 120;
    int card_h = 160;
    int card_x = display_w - card_w - 50;
    int card_y = (display_h - card_h) / 2;
    draw_sd_card_icon(card_x, card_y, card_w, card_h);

    // Instructions at bottom
    pax_draw_text(fb, COLOR_TEXT, pax_font_sky_mono, 14, 10, display_h - 45,
                  "Up/Down: Select   Enter: Confirm");
    pax_draw_text(fb, COLOR_TEXT, pax_font_sky_mono, 14, 10, display_h - 25,
                  "F1: Format   F3: Wipe   ESC: Exit");
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
