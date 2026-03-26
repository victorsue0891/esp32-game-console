#pragma once

#include <stdint.h>
#include "esp_err.h"

/**
 * Initialize battery ADC reading (GPIO15 / ADC2_CH4).
 * Uses a voltage divider; ADC2 is shared with WiFi.
 */
esp_err_t battery_init(void);

/**
 * Get battery level as percentage (0-100).
 * Returns last known value if ADC2 is busy (WiFi active).
 */
int battery_get_level(void);

/**
 * Get raw battery voltage in millivolts.
 */
int battery_get_voltage_mv(void);

/**
 * Pause ADC readings (call before WiFi starts).
 */
void battery_pause(void);

/**
 * Resume ADC readings (call after WiFi stops).
 */
void battery_resume(void);

/**
 * De-initialize battery ADC.
 */
void battery_deinit(void);
