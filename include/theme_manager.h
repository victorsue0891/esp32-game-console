#pragma once

#include "lvgl.h"

typedef enum {
    THEME_DARK = 0,
    THEME_LIGHT,
    THEME_RETRO,
    THEME_COUNT
} theme_id_t;

typedef struct {
    lv_color_t bg_primary;
    lv_color_t bg_secondary;
    lv_color_t text_primary;
    lv_color_t text_secondary;
    lv_color_t accent;
    lv_color_t list_bg;
    lv_color_t list_focus;
} theme_colors_t;

/**
 * Load theme preference from NVS. Call after nvs_flash_init().
 */
void theme_manager_init(void);

/**
 * Set and save the active theme.
 */
void theme_manager_set(theme_id_t id);

/**
 * Get the currently active theme ID.
 */
theme_id_t theme_manager_get(void);

/**
 * Get the color palette for the active theme.
 */
const theme_colors_t *theme_manager_colors(void);

/**
 * Get the display name for a theme.
 */
const char *theme_manager_name(theme_id_t id);
