#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * Initialize the SD card (SPI mode) and mount FAT filesystem.
 * @return ESP_OK on success.
 */
esp_err_t sd_card_init(void);

/**
 * Check if the SD card is mounted and accessible.
 */
bool sd_card_is_mounted(void);

/**
 * Runtime health check: verify SD card is still readable.
 * Returns false if card was removed or I/O error occurred.
 */
bool sd_card_check_health(void);

/**
 * Unmount and de-initialize the SD card.
 */
void sd_card_deinit(void);
