#pragma once

/**
 * Initialize LVGL library, display driver, and input driver.
 * Must be called after lcd_init() and button_init().
 */
void ui_init(void);

/**
 * Start the LVGL tick + handler task on Core 0.
 */
void ui_task_start(void);

/**
 * Acquire LVGL mutex (for thread-safe access from other tasks).
 */
void ui_lock(void);

/**
 * Release LVGL mutex.
 */
void ui_unlock(void);
