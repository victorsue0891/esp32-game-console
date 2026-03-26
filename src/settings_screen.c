#include "settings_screen.h"
#include "config.h"
#include "ui_manager.h"
#include "theme_manager.h"
#include "battery.h"
#include "wifi_manager.h"
#include "ota_update.h"
#include "perf_monitor.h"

#include "lvgl.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>

static const char *TAG = "settings";

static settings_back_cb_t back_cb = NULL;
static lv_obj_t *scr = NULL;

/* Widgets that need updating */
static lv_obj_t *theme_val_label = NULL;
static lv_obj_t *fps_val_label = NULL;
static lv_obj_t *bat_val_label = NULL;
static lv_obj_t *heap_val_label = NULL;
static lv_obj_t *fw_val_label = NULL;
static lv_obj_t *ota_status_label = NULL;
static lv_obj_t *ota_bar = NULL;

/* ---- OTA background task state ---- */
static volatile int ota_progress = -1;
static volatile bool ota_running = false;
static volatile bool ota_failed = false;
static char ota_fail_msg[64] = {0};

static void ota_progress_cb(int percent)
{
    ota_progress = percent;
}

/* ---- Timer to refresh dynamic values ---- */
static lv_timer_t *refresh_timer = NULL;

static void refresh_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!scr) return;

    /* Battery */
    if (bat_val_label) {
        lv_label_set_text_fmt(bat_val_label, "%d%% (%dmV)",
                              battery_get_level(), battery_get_voltage_mv());
    }
    /* Heap */
    if (heap_val_label) {
        int heap_kb = (int)(esp_get_free_heap_size() / 1024);
        lv_label_set_text_fmt(heap_val_label, "%d KB", heap_kb);
    }
    /* FPS */
    if (fps_val_label) {
        lv_label_set_text_fmt(fps_val_label, "%s",
                              perf_monitor_overlay_enabled() ? "ON" : "OFF");
    }
    /* OTA progress (updated from background task) */
    if (ota_progress >= 0 && ota_bar) {
        lv_bar_set_value(ota_bar, ota_progress, LV_ANIM_ON);
        if (ota_status_label) {
            lv_label_set_text_fmt(ota_status_label, "Downloading... %d%%", ota_progress);
        }
    }
    if (ota_failed && ota_status_label) {
        lv_label_set_text(ota_status_label, ota_fail_msg);
        ota_failed = false;
    }
}

/* ---- Helper: create a setting row (label + value) ---- */
static lv_obj_t *create_row(lv_obj_t *parent, const char *label_text,
                             lv_obj_t **out_val_label, const char *initial_val)
{
    const theme_colors_t *tc = theme_manager_colors();

    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 32);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(row, tc->list_focus, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_color(lbl, tc->text_primary, 0);

    lv_obj_t *val = lv_label_create(row);
    lv_label_set_text(val, initial_val);
    lv_obj_set_style_text_color(val, tc->accent, 0);

    if (out_val_label) *out_val_label = val;
    return row;
}

/* ---- Callbacks ---- */

static void on_theme_clicked(lv_event_t *e)
{
    (void)e;
    theme_id_t next = (theme_manager_get() + 1) % THEME_COUNT;
    theme_manager_set(next);

    /* Re-create settings screen with new theme */
    settings_back_cb_t cb = back_cb;
    settings_screen_hide();
    settings_screen_show(cb);
}

static void on_fps_clicked(lv_event_t *e)
{
    (void)e;
    perf_monitor_set_overlay(!perf_monitor_overlay_enabled());
    if (fps_val_label) {
        lv_label_set_text(fps_val_label,
                          perf_monitor_overlay_enabled() ? "ON" : "OFF");
    }
}

/* ---- OTA background task ---- */
static char ota_url_buf[256];

static void ota_task(void *arg)
{
    (void)arg;

    battery_pause();

    esp_err_t ret = wifi_manager_connect_from_config(ota_url_buf, sizeof(ota_url_buf));
    if (ret != ESP_OK) {
        snprintf(ota_fail_msg, sizeof(ota_fail_msg), "WiFi failed!");
        ota_failed = true;
        ota_running = false;
        battery_resume();
        vTaskDelete(NULL);
        return;
    }

    if (ota_url_buf[0] == '\0') {
        snprintf(ota_fail_msg, sizeof(ota_fail_msg), "No OTA_URL in wifi.cfg");
        ota_failed = true;
        wifi_manager_disconnect();
        ota_running = false;
        battery_resume();
        vTaskDelete(NULL);
        return;
    }

    ota_progress = 0;
    ret = ota_start_update(ota_url_buf, ota_progress_cb);

    /* If we reach here, OTA failed (success reboots) */
    snprintf(ota_fail_msg, sizeof(ota_fail_msg), "OTA failed: %s",
             esp_err_to_name(ret));
    ota_failed = true;
    ota_progress = -1;
    wifi_manager_disconnect();
    ota_running = false;
    battery_resume();
    vTaskDelete(NULL);
}

static void on_ota_clicked(lv_event_t *e)
{
    (void)e;
    if (ota_running) return; /* Already in progress */

    if (ota_status_label) {
        lv_label_set_text(ota_status_label, "Connecting WiFi...");
    }
    if (ota_bar) lv_obj_clear_flag(ota_bar, LV_OBJ_FLAG_HIDDEN);

    ota_running = true;
    ota_progress = -1;
    ota_failed = false;

    xTaskCreatePinnedToCore(ota_task, "ota_task", 8192, NULL, 3, NULL, 0);
}

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    settings_screen_hide();
    if (back_cb) back_cb();
}

void settings_screen_show(settings_back_cb_t cb)
{
    back_cb = cb;
    const theme_colors_t *tc = theme_manager_colors();

    ui_lock();

    /* Create a new screen */
    scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, tc->bg_primary, 0);

    /* Container with vertical flex */
    lv_obj_t *cont = lv_obj_create(scr);
    lv_obj_set_size(cont, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_style_pad_all(cont, 6, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_center(cont);

    /* Title */
    lv_obj_t *title = lv_label_create(cont);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS " Settings");
    lv_obj_set_style_text_color(title, tc->text_primary, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

    /* Separator */
    lv_obj_t *sep = lv_obj_create(cont);
    lv_obj_set_size(sep, LV_PCT(100), 2);
    lv_obj_set_style_bg_color(sep, tc->accent, 0);
    lv_obj_set_style_border_width(sep, 0, 0);

    lv_group_t *g = lv_group_get_default();

    /* ---- Theme row ---- */
    lv_obj_t *theme_row = create_row(cont, "Theme:", &theme_val_label,
                                     theme_manager_name(theme_manager_get()));
    lv_obj_add_event_cb(theme_row, on_theme_clicked, LV_EVENT_CLICKED, NULL);
    lv_group_add_obj(g, theme_row);
    lv_obj_add_flag(theme_row, LV_OBJ_FLAG_CLICKABLE);

    /* ---- FPS overlay row ---- */
    lv_obj_t *fps_row = create_row(cont, "FPS Overlay:", &fps_val_label,
                                   perf_monitor_overlay_enabled() ? "ON" : "OFF");
    lv_obj_add_event_cb(fps_row, on_fps_clicked, LV_EVENT_CLICKED, NULL);
    lv_group_add_obj(g, fps_row);
    lv_obj_add_flag(fps_row, LV_OBJ_FLAG_CLICKABLE);

    /* ---- OTA row ---- */
    lv_obj_t *ota_row = create_row(cont, "WiFi OTA:", NULL, "Start");
    lv_obj_add_event_cb(ota_row, on_ota_clicked, LV_EVENT_CLICKED, NULL);
    lv_group_add_obj(g, ota_row);
    lv_obj_add_flag(ota_row, LV_OBJ_FLAG_CLICKABLE);

    /* OTA status + progress bar (hidden initially) */
    ota_status_label = lv_label_create(cont);
    lv_label_set_text(ota_status_label, "");
    lv_obj_set_style_text_color(ota_status_label, tc->text_secondary, 0);

    ota_bar = lv_bar_create(cont);
    lv_obj_set_size(ota_bar, LV_PCT(90), 10);
    lv_bar_set_range(ota_bar, 0, 100);
    lv_bar_set_value(ota_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ota_bar, tc->bg_secondary, 0);
    lv_obj_set_style_bg_color(ota_bar, tc->accent, LV_PART_INDICATOR);
    lv_obj_add_flag(ota_bar, LV_OBJ_FLAG_HIDDEN);

    /* ---- Separator ---- */
    lv_obj_t *sep2 = lv_obj_create(cont);
    lv_obj_set_size(sep2, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(sep2, tc->text_secondary, 0);
    lv_obj_set_style_border_width(sep2, 0, 0);

    /* ---- Info: Battery ---- */
    char bat_str[32];
    snprintf(bat_str, sizeof(bat_str), "%d%% (%dmV)",
             battery_get_level(), battery_get_voltage_mv());
    create_row(cont, "Battery:", &bat_val_label, bat_str);

    /* ---- Info: Heap ---- */
    char heap_str[32];
    snprintf(heap_str, sizeof(heap_str), "%d KB",
             (int)(esp_get_free_heap_size() / 1024));
    create_row(cont, "Free Heap:", &heap_val_label, heap_str);

    /* ---- Info: FW version ---- */
    create_row(cont, "Firmware:", &fw_val_label, ota_get_running_version());

    /* ---- Back button ---- */
    lv_obj_t *back_row = create_row(cont, "", NULL, LV_SYMBOL_LEFT " Back");
    lv_obj_add_event_cb(back_row, on_back_clicked, LV_EVENT_CLICKED, NULL);
    lv_group_add_obj(g, back_row);
    lv_obj_add_flag(back_row, LV_OBJ_FLAG_CLICKABLE);

    /* Start refresh timer (2s interval) */
    refresh_timer = lv_timer_create(refresh_timer_cb, 2000, NULL);

    /* Load this screen */
    lv_scr_load(scr);

    ui_unlock();

    ESP_LOGI(TAG, "Settings screen shown");
}

void settings_screen_hide(void)
{
    ui_lock();

    if (refresh_timer) {
        lv_timer_del(refresh_timer);
        refresh_timer = NULL;
    }

    /* Remove settings objects from group */
    lv_group_t *g = lv_group_get_default();
    if (g) lv_group_remove_all_objs(g);

    if (scr) {
        lv_obj_del(scr);
        scr = NULL;
    }

    theme_val_label = NULL;
    fps_val_label = NULL;
    bat_val_label = NULL;
    heap_val_label = NULL;
    fw_val_label = NULL;
    ota_status_label = NULL;
    ota_bar = NULL;

    ui_unlock();
    ESP_LOGI(TAG, "Settings screen hidden");
}
