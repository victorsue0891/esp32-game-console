#include "wifi_manager.h"
#include "config.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "wifi_mgr";

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t wifi_event_group = NULL;
static esp_netif_t *sta_netif = NULL;
static bool wifi_inited = false;
static bool wifi_connected = false;
static int retry_count = 0;

/* ---- Event handler ---- */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (retry_count < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            retry_count++;
            ESP_LOGI(TAG, "Retry WiFi connect (%d/%d)", retry_count, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        }
        wifi_connected = false;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        retry_count = 0;
        wifi_connected = true;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_manager_init(void)
{
    if (wifi_inited) return ESP_OK;

    ESP_LOGI(TAG, "Initializing WiFi...");

    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    /* esp_event_loop_create_default may already be called; ignore error */
    esp_event_loop_create_default();

    sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t inst_any_id, inst_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler,
        NULL, &inst_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler,
        NULL, &inst_got_ip));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_inited = true;
    ESP_LOGI(TAG, "WiFi initialized (STA mode)");
    return ESP_OK;
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password)
{
    if (!wifi_inited) {
        esp_err_t ret = wifi_manager_init();
        if (ret != ESP_OK) return ret;
    }

    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    if (password && password[0]) {
        strncpy((char *)wifi_cfg.sta.password, password,
                sizeof(wifi_cfg.sta.password) - 1);
    }
    wifi_cfg.sta.threshold.authmode = password && password[0]
                                       ? WIFI_AUTH_WPA2_PSK
                                       : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));

    retry_count = 0;
    xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Block until connected or failed */
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected");
        return ESP_OK;
    }
    ESP_LOGE(TAG, "WiFi connection failed");
    esp_wifi_stop();
    return ESP_FAIL;
}

esp_err_t wifi_manager_connect_from_config(char *ota_url_out, size_t ota_url_size)
{
    char ssid[64] = {0};
    char pass[64] = {0};

    if (ota_url_out && ota_url_size > 0) ota_url_out[0] = '\0';

    FILE *f = fopen(WIFI_CONFIG_PATH, "r");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s", WIFI_CONFIG_PATH);
        return ESP_ERR_NOT_FOUND;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        /* Strip trailing newline */
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        nl = strchr(line, '\r');
        if (nl) *nl = '\0';

        if (strncmp(line, "SSID=", 5) == 0) {
            strncpy(ssid, line + 5, sizeof(ssid) - 1);
        } else if (strncmp(line, "PASS=", 5) == 0) {
            strncpy(pass, line + 5, sizeof(pass) - 1);
        } else if (strncmp(line, "OTA_URL=", 8) == 0 && ota_url_out) {
            strncpy(ota_url_out, line + 8, ota_url_size - 1);
            ota_url_out[ota_url_size - 1] = '\0';
        }
    }
    fclose(f);

    if (ssid[0] == '\0') {
        ESP_LOGE(TAG, "No SSID found in wifi.cfg");
        return ESP_ERR_INVALID_ARG;
    }

    return wifi_manager_connect(ssid, pass);
}

void wifi_manager_disconnect(void)
{
    if (!wifi_inited) return;
    esp_wifi_disconnect();
    esp_wifi_stop();
    wifi_connected = false;
    ESP_LOGI(TAG, "WiFi disconnected");
}

bool wifi_manager_is_connected(void)
{
    return wifi_connected;
}

void wifi_manager_deinit(void)
{
    if (!wifi_inited) return;
    wifi_manager_disconnect();
    esp_wifi_deinit();
    if (sta_netif) {
        esp_netif_destroy_default_wifi(sta_netif);
        sta_netif = NULL;
    }
    if (wifi_event_group) {
        vEventGroupDelete(wifi_event_group);
        wifi_event_group = NULL;
    }
    wifi_inited = false;
    ESP_LOGI(TAG, "WiFi de-initialized");
}
