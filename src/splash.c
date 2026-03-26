#include "splash.h"
#include "config.h"
#include "button.h"
#include "ui_manager.h"

#include "lvgl.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "splash";
static bool done = false;

void splash_show(void)
{
    ESP_LOGI(TAG, "Showing splash screen...");

    ui_lock();

    /* Create a full-screen container */
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    /* Try to load boot image from SD card */
    lv_obj_t *img = lv_img_create(scr);

    /* Use LVGL file system driver to load from SD:
     * LVGL expects "S:" prefix for SD card with our registered driver,
     * but we'll use the file path directly via a custom approach.
     * For simplicity, load as SJPG if available, else show text. */
    lv_img_set_src(img, BOOT_IMAGE_PATH);
    lv_obj_center(img);

    /* If image load fails, show a text fallback */
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "ESP32 Game Console\n\nPress START");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);

    ui_unlock();

    /* Wait for Start button press */
    ESP_LOGI(TAG, "Waiting for START button...");
    while (!button_is_pressed(BTN_ID_START)) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    /* Debounce: wait for release */
    while (button_is_pressed(BTN_ID_START)) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    /* Fade-out effect: dim screen by overlaying a black rectangle with increasing opacity */
    ui_lock();
    lv_obj_t *fade = lv_obj_create(scr);
    lv_obj_remove_style_all(fade);
    lv_obj_set_size(fade, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_style_bg_color(fade, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(fade, LV_OPA_TRANSP, 0);
    ui_unlock();

    for (int opa = 0; opa <= 255; opa += 15) {
        ui_lock();
        lv_obj_set_style_bg_opa(fade, opa, 0);
        ui_unlock();
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    /* Clean up splash objects */
    ui_lock();
    lv_obj_clean(scr);
    ui_unlock();

    done = true;
    ESP_LOGI(TAG, "Splash screen dismissed");
}

bool splash_is_done(void)
{
    return done;
}
