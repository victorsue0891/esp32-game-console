#include "volume_osd.h"
#include "config.h"
#include "audio.h"
#include "ui_manager.h"

#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "vol_osd";

static lv_obj_t *osd_bar = NULL;
static lv_obj_t *osd_label = NULL;
static int64_t last_interaction_us = 0;
static bool osd_visible = false;

void volume_osd_init(void)
{
    ui_lock();

    lv_obj_t *scr = lv_scr_act();

    /* Vertical bar on the left side of the screen */
    osd_bar = lv_bar_create(scr);
    lv_obj_set_size(osd_bar, 20, LCD_HEIGHT - 40);
    lv_obj_align(osd_bar, LV_ALIGN_LEFT_MID, 10, 0);
    lv_bar_set_range(osd_bar, VOLUME_MIN, VOLUME_MAX);
    lv_bar_set_value(osd_bar, audio_get_volume(), LV_ANIM_OFF);

    /* Semi-transparent style */
    lv_obj_set_style_bg_opa(osd_bar, LV_OPA_50, 0);
    lv_obj_set_style_bg_color(osd_bar, lv_color_make(0x40, 0x40, 0x40), 0);
    lv_obj_set_style_bg_opa(osd_bar, LV_OPA_80, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(osd_bar, lv_color_make(0x00, 0xCC, 0x66), LV_PART_INDICATOR);
    lv_obj_set_style_radius(osd_bar, 5, 0);

    /* Volume percentage label */
    osd_label = lv_label_create(scr);
    lv_obj_set_style_text_color(osd_label, lv_color_white(), 0);
    lv_obj_align_to(osd_label, osd_bar, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
    lv_label_set_text_fmt(osd_label, "%d%%", audio_get_volume());

    /* Initially hidden */
    lv_obj_add_flag(osd_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(osd_label, LV_OBJ_FLAG_HIDDEN);
    osd_visible = false;

    ui_unlock();

    ESP_LOGI(TAG, "Volume OSD initialized");
}

static void osd_show(void)
{
    int vol = audio_get_volume();

    ui_lock();
    lv_bar_set_value(osd_bar, vol, LV_ANIM_ON);
    lv_label_set_text_fmt(osd_label, "%d%%", vol);
    lv_obj_clear_flag(osd_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(osd_label, LV_OBJ_FLAG_HIDDEN);
    ui_unlock();

    osd_visible = true;
    last_interaction_us = esp_timer_get_time();
}

void volume_osd_up(void)
{
    int vol = audio_get_volume() + VOLUME_STEP;
    audio_set_volume(vol);
    osd_show();
}

void volume_osd_down(void)
{
    int vol = audio_get_volume() - VOLUME_STEP;
    audio_set_volume(vol);
    osd_show();
}

void volume_osd_tick(void)
{
    if (!osd_visible) return;

    int64_t now = esp_timer_get_time();
    int64_t elapsed_ms = (now - last_interaction_us) / 1000;

    if (elapsed_ms >= VOLUME_OSD_TIMEOUT_MS) {
        ui_lock();
        lv_obj_add_flag(osd_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(osd_label, LV_OBJ_FLAG_HIDDEN);
        ui_unlock();
        osd_visible = false;
        ESP_LOGD(TAG, "Volume OSD hidden (timeout)");
    }
}
