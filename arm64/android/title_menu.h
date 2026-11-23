#pragma once

typedef enum {
    RX_TITLE_PROMPT, RX_TITLE_MENU, RX_TITLE_DIFFICULTY, RX_TITLE_OPTIONS
} RxTitleScreen;

void rx_title_menu_init(void);
// Returns the selected original difficulty index when New Game is confirmed.
int rx_title_menu_update(RxTitleScreen *screen);
unsigned rx_title_pointer_button(RxTitleScreen screen, float x, float y);
int rx_title_options_draw(int interactive);
void rx_title_read_settings(const char *directory);
int rx_title_write_settings(const char *directory);
