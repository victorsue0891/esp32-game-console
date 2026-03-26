#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * Initialize WiFi subsystem (STA mode). Call once at boot.
 */
esp_err_t wifi_manager_init(void);

/**
 * Connect to an AP with given SSID/password.
 * Blocks until connected or timeout.
 */
esp_err_t wifi_manager_connect(const char *ssid, const char *password);

/**
 * Read wifi.cfg from SD card and connect.
 * File format: SSID=... / PASS=... / OTA_URL=... (one per line).
 * Populates ota_url_out (max ota_url_size) if OTA_URL is found.
 */
esp_err_t wifi_manager_connect_from_config(char *ota_url_out, size_t ota_url_size);

/**
 * Disconnect from AP and stop WiFi.
 */
void wifi_manager_disconnect(void);

/**
 * Check if WiFi is currently connected.
 */
bool wifi_manager_is_connected(void);

/**
 * De-initialize WiFi subsystem.
 */
void wifi_manager_deinit(void);
