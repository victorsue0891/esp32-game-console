#pragma once

/**
 * Callback type when a ROM is selected.
 * @param path  Full path to the selected ROM file.
 */
typedef void (*game_launch_cb_t)(const char *path);

/**
 * Create and show the game launcher UI.
 * Left panel: ROM file list from SD card.
 * Right panel: Preview image for selected ROM.
 * @param cb  Callback invoked when user presses A on a ROM entry.
 */
void game_launcher_show(game_launch_cb_t cb);

/**
 * Destroy the game launcher UI and free resources.
 */
void game_launcher_hide(void);

/**
 * Invalidate the cached ROM list so next show() re-scans SD card.
 */
void game_launcher_invalidate_cache(void);
