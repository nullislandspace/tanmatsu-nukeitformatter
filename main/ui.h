#pragma once

#include "pax_gfx.h"

// Initialize UI with framebuffer reference
void ui_init(pax_buf_t *framebuffer);

// Draw main menu (selected: 0=Format, 1=Exit)
void ui_draw_main_menu(int selected);

// Draw confirmation dialog
void ui_draw_confirm(const char *title, const char *message);

// Draw "Please wait" screen
void ui_draw_wait(const char *message);

// Draw error screen
void ui_draw_error(const char *title, const char *message);

// Draw success screen
void ui_draw_success(const char *message);

// Blit framebuffer to display
void ui_blit(void);
