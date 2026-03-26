#pragma once

/**
 * Callback when user exits settings.
 */
typedef void (*settings_back_cb_t)(void);

/**
 * Show the settings screen (replaces current UI).
 * @param back_cb  Called when user presses Back.
 */
void settings_screen_show(settings_back_cb_t back_cb);

/**
 * Hide and clean up the settings screen.
 */
void settings_screen_hide(void);
