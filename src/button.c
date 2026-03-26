#include "button.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "button";

/* GPIO mapping indexed by button_id_t */
static const gpio_num_t btn_gpio[BTN_ID_MAX] = {
    [BTN_ID_UP]       = BTN_UP,
    [BTN_ID_DOWN]     = BTN_DOWN,
    [BTN_ID_LEFT]     = BTN_LEFT,
    [BTN_ID_RIGHT]    = BTN_RIGHT,
    [BTN_ID_A]        = BTN_A,
    [BTN_ID_B]        = BTN_B,
    [BTN_ID_START]    = BTN_START,
    [BTN_ID_SELECT]   = BTN_SELECT,
    [BTN_ID_VOL_UP]   = BTN_VOL_UP,
    [BTN_ID_VOL_DOWN] = BTN_VOL_DOWN,
};

/* Debounce state */
static bool btn_state[BTN_ID_MAX];
static uint8_t debounce_cnt[BTN_ID_MAX];
static button_event_cb_t event_cb = NULL;

#define DEBOUNCE_THRESHOLD 3
#define DEBOUNCE_PERIOD_MS 5  /* Debounce check interval after ISR fires */

/* ISR event queue */
static QueueHandle_t btn_evt_queue = NULL;
static volatile bool isr_wakeup = false;

/* ISR: any button edge sends a wakeup to the debounce task */
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    (void)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    uint8_t dummy = 1;
    xQueueSendFromISR(btn_evt_queue, &dummy, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

void button_init(void)
{
    /* Configure all GPIOs as input with pull-up, interrupt on any edge */
    for (int i = 0; i < BTN_ID_MAX; i++) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << btn_gpio[i]),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_ANYEDGE,
        };
        gpio_config(&io_conf);
    }

    /* Create event queue and install ISR service */
    btn_evt_queue = xQueueCreate(16, sizeof(uint8_t));
    gpio_install_isr_service(0);

    for (int i = 0; i < BTN_ID_MAX; i++) {
        gpio_isr_handler_add(btn_gpio[i], gpio_isr_handler, NULL);
    }

    memset(btn_state, 0, sizeof(btn_state));
    memset(debounce_cnt, 0, sizeof(debounce_cnt));
    ESP_LOGI(TAG, "Buttons initialized (ISR + debounce)");
}

bool button_is_pressed(button_id_t btn)
{
    if (btn >= BTN_ID_MAX) return false;
    return gpio_get_level(btn_gpio[btn]) == 0;
}

uint16_t button_get_state(void)
{
    uint16_t mask = 0;
    for (int i = 0; i < BTN_ID_MAX; i++) {
        if (btn_state[i]) {
            mask |= (1 << i);
        }
    }
    return mask;
}

void button_register_cb(button_event_cb_t cb)
{
    event_cb = cb;
}

/*
 * Debounce task: sleeps until ISR wakes it via queue, then reads
 * GPIOs for a few debounce cycles before going back to sleep.
 */
static void button_debounce_task(void *arg)
{
    uint8_t dummy;

    while (1) {
        /* Block until an ISR fires (or timeout for held-key repeat) */
        if (xQueueReceive(btn_evt_queue, &dummy, pdMS_TO_TICKS(50))) {
            /* Drain any extra ISR events that bunched up */
            while (xQueueReceive(btn_evt_queue, &dummy, 0)) {}
        }

        /* Run debounce passes */
        bool any_unstable = true;
        int passes = 0;
        while (any_unstable && passes < 8) {
            any_unstable = false;
            for (int i = 0; i < BTN_ID_MAX; i++) {
                bool raw = (gpio_get_level(btn_gpio[i]) == 0);

                if (raw != btn_state[i]) {
                    debounce_cnt[i]++;
                    any_unstable = true;
                    if (debounce_cnt[i] >= DEBOUNCE_THRESHOLD) {
                        btn_state[i] = raw;
                        debounce_cnt[i] = 0;
                        if (event_cb) {
                            event_cb((button_id_t)i, raw);
                        }
                    }
                } else {
                    debounce_cnt[i] = 0;
                }
            }
            if (any_unstable) {
                vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_PERIOD_MS));
            }
            passes++;
        }
    }
}

void button_task_start(void)
{
    xTaskCreatePinnedToCore(button_debounce_task, "btn_dbg", 3072, NULL, 5, NULL, 0);
    ESP_LOGI(TAG, "Button ISR debounce task started on Core 0");
}
