/**
 * ESP32-S3 Retro Handheld Game Console
 * Main entry point
 *
 * Core 0: System/UI — LVGL rendering, button scanning, volume OSD
 * Core 1: Emulator  — NES/GB emulation, I2S audio output
 */

#include "config.h"
#include "button.h"
#include "lcd_driver.h"
#include "sd_card.h"
#include "audio.h"
#include "ui_manager.h"
#include "splash.h"
#include "game_launcher.h"
#include "volume_osd.h"
#include "emulator.h"
#include "battery.h"
#include "theme_manager.h"
#include "perf_monitor.h"
#include "pause_menu.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"

static const char *TAG = "main";

/* Forward declarations */
static void on_game_selected(const char *rom_path);
static void on_pause_resume(void);
static void on_pause_exit(void);

/* ---- Button Event Handler ---- */
static void on_button_event(button_id_t btn, bool pressed)
{
    if (!pressed) return;

    /* Volume control (works in any state) */
    if (btn == BTN_ID_VOL_UP) {
        volume_osd_up();
        return;
    }
    if (btn == BTN_ID_VOL_DOWN) {
        volume_osd_down();
        return;
    }

    /* Ignore game buttons while pause menu is visible */
    if (pause_menu_is_visible()) return;

    /* If emulator is running, feed input and handle combo keys */
    if (emulator_is_running()) {
        if (btn == BTN_ID_SELECT && button_is_pressed(BTN_ID_START)) {
            if (emulator_is_paused()) {
                /* Select + Start while paused = exit to menu */
                ESP_LOGI(TAG, "Exiting emulator...");
                emulator_stop();
                game_launcher_show(on_game_selected);
            } else {
                /* Select + Start while playing = show pause menu */
                emulator_pause();
                pause_menu_show(on_pause_resume, on_pause_exit);
            }
            return;
        }
        if (btn == BTN_ID_START && emulator_is_paused()) {
            /* START alone while paused = resume */
            emulator_resume();
            return;
        }
        emulator_set_input(button_get_state());
    }
}

/* ---- Pause Menu Callbacks ---- */
static void on_pause_resume(void)
{
    emulator_resume();
}

static void on_pause_exit(void)
{
    emulator_stop();
    game_launcher_show(on_game_selected);
}

/* ---- Game Launch Callback ---- */
static void on_game_selected(const char *rom_path)
{
    ESP_LOGI(TAG, "Game selected: %s", rom_path);
    game_launcher_hide();
    emulator_start(rom_path);
}

/* ---- Volume OSD Tick Task ---- */
static void volume_tick_task(void *arg)
{
    while (1) {
        volume_osd_tick();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ---- Main Application ---- */
void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-S3 Retro Handheld Game Console ===");
    ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());

    /* 0. Initialize NVS (needed for theme/WiFi) */
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* 1. Initialize hardware drivers */
    button_init();
    lcd_init();

    esp_err_t sd_ret = sd_card_init();
    if (sd_ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card init failed! Some features unavailable.");
    }

    audio_init();
    battery_init();

    /* 2. Initialize LVGL UI system and subsystems */
    ui_init();
    ui_task_start();
    theme_manager_init();
    perf_monitor_init();

    /* 3. Start button polling task */
    button_register_cb(on_button_event);
    button_task_start();

    /* All critical hardware initialised — cancel OTA rollback watchdog.
     * Without this call, ESP-IDF rolls back to the previous firmware on
     * the next reboot when the new image was flashed via OTA. */
    esp_ota_mark_app_valid_cancel_rollback();

    /* 4. Show splash screen (blocks until Start pressed) */
    splash_show();

    /* 5. Initialize volume OSD */
    volume_osd_init();

    /* 6. Start volume OSD timeout tick */
    xTaskCreatePinnedToCore(volume_tick_task, "vol_tick", 2048, NULL, 2, NULL, 0);

    /* 7. Show game launcher menu */
    game_launcher_show(on_game_selected);

    /* 8. Main task can idle — all work is in FreeRTOS tasks */
    ESP_LOGI(TAG, "System ready. Entering idle loop.");
    while (1) {
        /* Feed emulator input continuously if running */
        if (emulator_is_running()) {
            emulator_set_input(button_get_state());
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
