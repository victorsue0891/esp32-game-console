#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Button ID enumeration */
typedef enum {
    BTN_ID_UP = 0,
    BTN_ID_DOWN,
    BTN_ID_LEFT,
    BTN_ID_RIGHT,
    BTN_ID_A,
    BTN_ID_B,
    BTN_ID_START,
    BTN_ID_SELECT,
    BTN_ID_VOL_UP,
    BTN_ID_VOL_DOWN,
    BTN_ID_MAX
} button_id_t;

/* Button event callback */
typedef void (*button_event_cb_t)(button_id_t btn, bool pressed);

/**
 * Initialize all button GPIOs with internal pull-up and debounce.
 */
void button_init(void);

/**
 * Read current state of a button (true = pressed).
 */
bool button_is_pressed(button_id_t btn);

/**
 * Get a bitmask of all currently-pressed buttons.
 */
uint16_t button_get_state(void);

/**
 * Register a callback for button events.
 */
void button_register_cb(button_event_cb_t cb);

/**
 * Start the button polling task (runs on Core 0).
 */
void button_task_start(void);
