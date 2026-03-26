#include "ui_manager.h"
#include "config.h"
#include "lcd_driver.h"
#include "button.h"

#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "ui";

#define LV_BUF_LINES  40
#define UI_TASK_STACK  8192

static lv_disp_draw_buf_t draw_buf_desc;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;
static lv_color_t *buf1 = NULL;
static lv_color_t *buf2 = NULL;

static SemaphoreHandle_t lvgl_mutex = NULL;

/* ---- Display flush callback ---- */
static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                           lv_color_t *color_p)
{
    lcd_flush_area(area->x1, area->y1, area->x2, area->y2,
                   (const uint16_t *)color_p);
    lv_disp_flush_ready(drv);
}

/* ---- Input read callback ---- */
static uint32_t last_key = 0;
static lv_indev_state_t last_state = LV_INDEV_STATE_REL;

static void indev_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    data->state = LV_INDEV_STATE_REL;
    data->key = 0;

    if (button_is_pressed(BTN_ID_UP)) {
        data->key = LV_KEY_UP;
        data->state = LV_INDEV_STATE_PR;
    } else if (button_is_pressed(BTN_ID_DOWN)) {
        data->key = LV_KEY_DOWN;
        data->state = LV_INDEV_STATE_PR;
    } else if (button_is_pressed(BTN_ID_LEFT)) {
        data->key = LV_KEY_LEFT;
        data->state = LV_INDEV_STATE_PR;
    } else if (button_is_pressed(BTN_ID_RIGHT)) {
        data->key = LV_KEY_RIGHT;
        data->state = LV_INDEV_STATE_PR;
    } else if (button_is_pressed(BTN_ID_A)) {
        data->key = LV_KEY_ENTER;
        data->state = LV_INDEV_STATE_PR;
    } else if (button_is_pressed(BTN_ID_B)) {
        data->key = LV_KEY_ESC;
        data->state = LV_INDEV_STATE_PR;
    }

    last_key = data->key;
    last_state = data->state;
}

/* ---- LVGL tick timer callback ---- */
static void lv_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LV_TICK_PERIOD_MS);
}

void ui_init(void)
{
    ESP_LOGI(TAG, "Initializing LVGL...");

    lv_init();

    /* Allocate draw buffers */
    buf1 = heap_caps_malloc(LCD_WIDTH * LV_BUF_LINES * sizeof(lv_color_t),
                            MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    buf2 = heap_caps_malloc(LCD_WIDTH * LV_BUF_LINES * sizeof(lv_color_t),
                            MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

    lv_disp_draw_buf_init(&draw_buf_desc, buf1, buf2,
                          LCD_WIDTH * LV_BUF_LINES);

    /* Display driver setup */
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_WIDTH;
    disp_drv.ver_res = LCD_HEIGHT;
    disp_drv.flush_cb = disp_flush_cb;
    disp_drv.draw_buf = &draw_buf_desc;
    lv_disp_drv_register(&disp_drv);

    /* Input (keypad) driver setup */
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_KEYPAD;
    indev_drv.read_cb = indev_read_cb;
    lv_indev_t *indev = lv_indev_drv_register(&indev_drv);

    /* Create default group and bind to input device */
    lv_group_t *g = lv_group_create();
    lv_group_set_default(g);
    lv_indev_set_group(indev, g);

    /* LVGL mutex */
    lvgl_mutex = xSemaphoreCreateMutex();

    /* Tick timer */
    const esp_timer_create_args_t timer_args = {
        .callback = lv_tick_cb,
        .name = "lv_tick",
    };
    esp_timer_handle_t tick_timer;
    esp_timer_create(&timer_args, &tick_timer);
    esp_timer_start_periodic(tick_timer, LV_TICK_PERIOD_MS * 1000);

    ESP_LOGI(TAG, "LVGL initialized");
}

static void ui_task(void *arg)
{
    while (1) {
        ui_lock();
        lv_timer_handler();
        ui_unlock();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void ui_task_start(void)
{
    xTaskCreatePinnedToCore(ui_task, "lvgl_task", UI_TASK_STACK, NULL, 4, NULL, 0);
    ESP_LOGI(TAG, "LVGL task started on Core 0");
}

void ui_lock(void)
{
    if (lvgl_mutex) {
        xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
    }
}

void ui_unlock(void)
{
    if (lvgl_mutex) {
        xSemaphoreGive(lvgl_mutex);
    }
}
