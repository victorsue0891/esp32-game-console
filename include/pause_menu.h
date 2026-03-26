#pragma once

#include <stdbool.h>

/**
 * Callback when user requests to resume the game.
 */
typedef void (*pause_menu_resume_cb_t)(void);

/**
 * Callback when user requests to exit to launcher.
 */
typedef void (*pause_menu_exit_cb_t)(void);

/**
 * Show the in-game pause menu overlay.
 * @param resume_cb  Called when user selects Resume.
 * @param exit_cb    Called when user selects Exit.
 */
void pause_menu_show(pause_menu_resume_cb_t resume_cb,
                     pause_menu_exit_cb_t exit_cb);

/**
 * Hide and destroy the pause menu.
 */
void pause_menu_hide(void);

/**
 * Check if pause menu is currently visible.
 */
bool pause_menu_is_visible(void);
