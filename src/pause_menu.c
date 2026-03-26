#include "pause_menu.h"
#include "config.h"
#include "ui_manager.h"
#include "theme_manager.h"
#include "emulator.h"

#include "lvgl.h"
#include "esp_log.h"

#include <stdbool.h>

static const char *TAG = "pause_menu";

static lv_obj_t *overlay = NULL;
static pause_menu_resume_cb_t resume_cb = NULL;
static pause_menu_exit_cb_t exit_cb = NULL;
static bool visible = false;

/* ---- Callbacks ---- */

static void on_resume(lv_event_t *e)
{
    (void)e;
    pause_menu_hide();
    if (resume_cb) resume_cb();
}

static void on_save(lv_event_t *e)
{
    (void)e;
    bool ok = emulator_save_state();
    ESP_LOGI(TAG, "Save state: %s", ok ? "OK" : "FAILED");

    /* Flash brief feedback on the button */
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    if (lbl) {
        lv_label_set_text(lbl, ok ? "Saved!" : "Save Failed!");
    }
}

static void on_load(lv_event_t *e)
{
    (void)e;
    bool ok = emulator_load_state();
    ESP_LOGI(TAG, "Load state: %s", ok ? "OK" : "FAILED");

    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    if (lbl) {
        lv_label_set_text(lbl, ok ? "Loaded!" : "No Save!");
    }
}

static void on_exit_game(lv_event_t *e)
{
    (void)e;
    pause_menu_hide();
    if (exit_cb) exit_cb();
}

void pause_menu_show(pause_menu_resume_cb_t r_cb, pause_menu_exit_cb_t e_cb)
{
    if (visible) return;
    resume_cb = r_cb;
    exit_cb = e_cb;

    const theme_colors_t *tc = theme_manager_colors();

    ui_lock();
    lv_obj_t *scr = lv_scr_act();

    /* Semi-transparent overlay covering entire screen */
    overlay = lv_obj_create(scr);
    lv_obj_set_size(overlay, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_center(overlay);

    /* Central panel */
    lv_obj_t *panel = lv_obj_create(overlay);
    lv_obj_set_size(panel, 180, 200);
    lv_obj_set_style_bg_color(panel, tc->bg_primary, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_90, 0);
    lv_obj_set_style_border_color(panel, tc->accent, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, 8, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_center(panel);

    /* Title */
    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, LV_SYMBOL_PAUSE " PAUSED");
    lv_obj_set_style_text_color(title, tc->accent, 0);

    lv_group_t *g = lv_group_get_default();

    /* Resume button */
    lv_obj_t *btn_resume = lv_btn_create(panel);
    lv_obj_set_size(btn_resume, 140, 32);
    lv_obj_set_style_bg_color(btn_resume, tc->list_bg, 0);
    lv_obj_set_style_bg_color(btn_resume, tc->list_focus, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(btn_resume, on_resume, LV_EVENT_CLICKED, NULL);
    lv_group_add_obj(g, btn_resume);
    lv_obj_t *lbl1 = lv_label_create(btn_resume);
    lv_label_set_text(lbl1, LV_SYMBOL_PLAY " Resume");
    lv_obj_set_style_text_color(lbl1, tc->text_primary, 0);
    lv_obj_center(lbl1);

    /* Save button */
    lv_obj_t *btn_save = lv_btn_create(panel);
    lv_obj_set_size(btn_save, 140, 32);
    lv_obj_set_style_bg_color(btn_save, tc->list_bg, 0);
    lv_obj_set_style_bg_color(btn_save, tc->list_focus, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(btn_save, on_save, LV_EVENT_CLICKED, NULL);
    lv_group_add_obj(g, btn_save);
    lv_obj_t *lbl2 = lv_label_create(btn_save);
    lv_label_set_text(lbl2, LV_SYMBOL_SAVE " Save State");
    lv_obj_set_style_text_color(lbl2, tc->text_primary, 0);
    lv_obj_center(lbl2);

    /* Load button */
    lv_obj_t *btn_load = lv_btn_create(panel);
    lv_obj_set_size(btn_load, 140, 32);
    lv_obj_set_style_bg_color(btn_load, tc->list_bg, 0);
    lv_obj_set_style_bg_color(btn_load, tc->list_focus, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(btn_load, on_load, LV_EVENT_CLICKED, NULL);
    lv_group_add_obj(g, btn_load);
    lv_obj_t *lbl3 = lv_label_create(btn_load);
    if (emulator_has_save_state()) {
        lv_label_set_text(lbl3, LV_SYMBOL_UPLOAD " Load State");
    } else {
        lv_label_set_text(lbl3, "(No Save)");
    }
    lv_obj_set_style_text_color(lbl3, tc->text_primary, 0);
    lv_obj_center(lbl3);

    /* Exit button */
    lv_obj_t *btn_exit = lv_btn_create(panel);
    lv_obj_set_size(btn_exit, 140, 32);
    lv_obj_set_style_bg_color(btn_exit, lv_color_make(0x80, 0x20, 0x20), 0);
    lv_obj_set_style_bg_color(btn_exit, lv_color_make(0xC0, 0x30, 0x30), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(btn_exit, on_exit_game, LV_EVENT_CLICKED, NULL);
    lv_group_add_obj(g, btn_exit);
    lv_obj_t *lbl4 = lv_label_create(btn_exit);
    lv_label_set_text(lbl4, LV_SYMBOL_CLOSE " Exit Game");
    lv_obj_set_style_text_color(lbl4, lv_color_white(), 0);
    lv_obj_center(lbl4);

    ui_unlock();
    visible = true;

    ESP_LOGI(TAG, "Pause menu shown");
}

void pause_menu_hide(void)
{
    if (!visible) return;

    ui_lock();
    /* Remove pause menu objects from group */
    lv_group_t *g = lv_group_get_default();
    if (g) lv_group_remove_all_objs(g);

    if (overlay) {
        lv_obj_del(overlay);
        overlay = NULL;
    }
    ui_unlock();

    visible = false;
    ESP_LOGI(TAG, "Pause menu hidden");
}

bool pause_menu_is_visible(void)
{
    return visible;
}
