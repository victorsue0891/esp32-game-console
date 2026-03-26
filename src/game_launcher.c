#include "game_launcher.h"
#include "config.h"
#include "ui_manager.h"
#include "theme_manager.h"
#include "settings_screen.h"

#include "lvgl.h"
#include "esp_log.h"

#include <dirent.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "launcher";

static lv_obj_t *cont = NULL;       /* Main container */
static lv_obj_t *rom_list = NULL;   /* Left: ROM list */
static lv_obj_t *preview = NULL;    /* Right: preview image */
static game_launch_cb_t launch_cb = NULL;

/* Store ROM paths for selection */
#define MAX_ROMS 128
#define ROM_PATH_MAX 300
static char rom_paths[MAX_ROMS][ROM_PATH_MAX];
static int rom_count = 0;
static bool rom_cache_valid = false; /* true = skip rescan */

/* ---- Scan directory for ROM files ---- */
static int scan_roms(const char *dir_path, const char *ext)
{
    DIR *dir = opendir(dir_path);
    if (!dir) {
        ESP_LOGW(TAG, "Cannot open directory: %s", dir_path);
        return 0;
    }

    struct dirent *entry;
    int count = 0;
    size_t ext_len = strlen(ext);

    while ((entry = readdir(dir)) != NULL && rom_count < MAX_ROMS) {
        size_t name_len = strlen(entry->d_name);
        if (name_len > ext_len &&
            strcasecmp(entry->d_name + name_len - ext_len, ext) == 0) {
            snprintf(rom_paths[rom_count], ROM_PATH_MAX,
                     "%s/%s", dir_path, entry->d_name);
            rom_count++;
            count++;
        }
    }

    closedir(dir);
    return count;
}

/* ---- Update preview image for selected ROM ---- */
static void update_preview(int index)
{
    if (index < 0 || index >= rom_count) return;

    /* Build preview path: replace extension with .jpg */
    char preview_path[260];
    strncpy(preview_path, rom_paths[index], sizeof(preview_path) - 1);
    preview_path[sizeof(preview_path) - 1] = '\0';

    char *dot = strrchr(preview_path, '.');
    if (dot && (size_t)(dot - preview_path) + 5 <= sizeof(preview_path)) {
        strcpy(dot, ".jpg");
    } else {
        return; /* Cannot build preview path safely */
    }

    /* Check if file exists */
    FILE *f = fopen(preview_path, "r");
    if (f) {
        fclose(f);
        lv_img_set_src(preview, preview_path);
    } else {
        lv_img_set_src(preview, NULL);
    }
}

/* ---- List focus change callback ---- */
static void list_focus_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *list_obj = lv_obj_get_parent(btn);

    /* Find index of focused button */
    uint32_t child_cnt = lv_obj_get_child_cnt(list_obj);
    for (uint32_t i = 0; i < child_cnt; i++) {
        if (lv_obj_get_child(list_obj, i) == btn) {
            update_preview((int)i);
            break;
        }
    }
}

/* ---- List item click callback (A button = Enter) ---- */
static void list_click_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *list_obj = lv_obj_get_parent(btn);

    uint32_t child_cnt = lv_obj_get_child_cnt(list_obj);
    for (uint32_t i = 0; i < child_cnt; i++) {
        if (lv_obj_get_child(list_obj, i) == btn) {
            if (launch_cb && i < (uint32_t)rom_count) {
                ESP_LOGI(TAG, "Launching ROM: %s", rom_paths[i]);
                launch_cb(rom_paths[i]);
            }
            break;
        }
    }
}

void game_launcher_invalidate_cache(void)
{
    rom_cache_valid = false;
}

/* ---- Settings integration ---- */
static void on_settings_back(void);

static void on_settings_clicked(lv_event_t *e)
{
    (void)e;
    game_launcher_hide();
    settings_screen_show(on_settings_back);
}

static void on_settings_back(void)
{
    game_launcher_show(launch_cb);
}

void game_launcher_show(game_launch_cb_t cb)
{
    launch_cb = cb;

    if (!rom_cache_valid) {
        rom_count = 0;
        ESP_LOGI(TAG, "Scanning ROMs...");
        int nes_count = scan_roms(ROM_NES_PATH, ".nes");
        int gb_count  = scan_roms(ROM_GB_PATH, ".gb");
        ESP_LOGI(TAG, "Found %d NES + %d GB ROMs (%d total)",
                 nes_count, gb_count, rom_count);
        rom_cache_valid = true;
    } else {
        ESP_LOGI(TAG, "Using cached ROM list (%d entries)", rom_count);
    }

    const theme_colors_t *tc = theme_manager_colors();

    ui_lock();

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, tc->bg_primary, 0);

    /* Main container: horizontal flex layout */
    cont = lv_obj_create(scr);
    lv_obj_set_size(cont, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_center(cont);

    /* Left panel: ROM list (60% width) */
    int list_w = (LCD_WIDTH * 60) / 100;
    rom_list = lv_list_create(cont);
    lv_obj_set_size(rom_list, list_w, LCD_HEIGHT);
    lv_obj_set_style_bg_color(rom_list, tc->list_bg, 0);
    lv_obj_set_style_border_width(rom_list, 0, 0);
    lv_obj_set_style_pad_row(rom_list, 2, 0);

    /* Right panel: Preview image (40% width) */
    int preview_w = LCD_WIDTH - list_w;
    lv_obj_t *right_panel = lv_obj_create(cont);
    lv_obj_set_size(right_panel, preview_w, LCD_HEIGHT);
    lv_obj_set_style_bg_color(right_panel, tc->bg_secondary, 0);
    lv_obj_set_style_border_width(right_panel, 0, 0);
    lv_obj_set_flex_flow(right_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right_panel, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    preview = lv_img_create(right_panel);
    lv_obj_center(preview);

    /* "No preview" label */
    lv_obj_t *no_preview_lbl = lv_label_create(right_panel);
    lv_label_set_text(no_preview_lbl, "No Preview");
    lv_obj_set_style_text_color(no_preview_lbl, tc->text_secondary, 0);

    /* Populate ROM list */
    lv_group_t *g = lv_group_get_default();

    for (int i = 0; i < rom_count; i++) {
        /* Extract filename from path */
        const char *name = strrchr(rom_paths[i], '/');
        name = name ? name + 1 : rom_paths[i];

        lv_obj_t *btn = lv_list_add_btn(rom_list, NULL, name);
        lv_obj_set_style_bg_color(btn, tc->list_focus, LV_STATE_FOCUSED);
        lv_obj_set_style_text_color(btn, tc->text_primary, 0);
        lv_obj_add_event_cb(btn, list_focus_cb, LV_EVENT_FOCUSED, NULL);
        lv_obj_add_event_cb(btn, list_click_cb, LV_EVENT_CLICKED, NULL);
        lv_group_add_obj(g, btn);
    }

    if (rom_count == 0) {
        lv_obj_t *empty_label = lv_label_create(rom_list);
        lv_label_set_text(empty_label, "No ROMs found.\n\n"
                          "Place .nes files in:\n /roms/nes/\n\n"
                          "Place .gb files in:\n /roms/gb/");
        lv_obj_set_style_text_color(empty_label, tc->text_secondary, 0);
    }

    /* Settings entry at the bottom of the list */
    lv_obj_t *settings_btn = lv_list_add_btn(rom_list, LV_SYMBOL_SETTINGS, "Settings");
    lv_obj_set_style_bg_color(settings_btn, tc->list_focus, LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(settings_btn, tc->accent, 0);
    lv_obj_add_event_cb(settings_btn, on_settings_clicked, LV_EVENT_CLICKED, NULL);
    lv_group_add_obj(g, settings_btn);

    ui_unlock();

    ESP_LOGI(TAG, "Game launcher displayed");
}

void game_launcher_hide(void)
{
    ui_lock();
    if (cont) {
        lv_obj_del(cont);
        cont = NULL;
        rom_list = NULL;
        preview = NULL;
    }
    ui_unlock();
    /* Keep rom_paths/rom_count cached; only invalidate on explicit request */
    ESP_LOGI(TAG, "Game launcher hidden");
}
