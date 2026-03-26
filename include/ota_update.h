#pragma once

#include "esp_err.h"

/**
 * Progress callback: percent 0-100.
 */
typedef void (*ota_progress_cb_t)(int percent);

/**
 * Download firmware from HTTP URL and flash to the next OTA partition.
 * Reboots on success. Blocks until complete or error.
 * @param url           HTTP URL of the firmware binary.
 * @param progress_cb   Optional progress callback (may be NULL).
 * @return ESP_OK on success (will reboot), or error code on failure.
 */
esp_err_t ota_start_update(const char *url, ota_progress_cb_t progress_cb);

/**
 * Get the running firmware version string.
 */
const char *ota_get_running_version(void);
