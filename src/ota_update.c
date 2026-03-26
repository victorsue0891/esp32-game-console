#include "ota_update.h"

#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "ota";

#define OTA_BUF_SIZE 4096

esp_err_t ota_start_update(const char *url, ota_progress_cb_t progress_cb)
{
    if (!url || url[0] == '\0') {
        ESP_LOGE(TAG, "OTA URL is empty");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting OTA from: %s", url);

    esp_http_client_config_t http_cfg = {
        .url = url,
        .timeout_ms = 30000,
        .keep_alive_enable = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        ESP_LOGE(TAG, "HTTP client init failed");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length <= 0) {
        ESP_LOGE(TAG, "Invalid content length: %d", content_length);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Firmware size: %d bytes", content_length);

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "No OTA partition available");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Writing to partition: %s (offset 0x%08lx)",
             update_partition->label, (unsigned long)update_partition->address);

    esp_ota_handle_t ota_handle;
    err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return err;
    }

    char *buf = malloc(OTA_BUF_SIZE);
    if (!buf) {
        esp_ota_abort(ota_handle);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    int total_read = 0;
    int last_percent = -1;

    while (1) {
        int read_len = esp_http_client_read(client, buf, OTA_BUF_SIZE);
        if (read_len < 0) {
            ESP_LOGE(TAG, "HTTP read error");
            err = ESP_FAIL;
            break;
        }
        if (read_len == 0) {
            /* Connection closed — check if all data received */
            if (esp_http_client_is_complete_data_received(client)) {
                break;
            }
            ESP_LOGE(TAG, "Connection closed prematurely");
            err = ESP_FAIL;
            break;
        }

        err = esp_ota_write(ota_handle, buf, (size_t)read_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            break;
        }

        total_read += read_len;

        if (progress_cb && content_length > 0) {
            int pct = total_read * 100 / content_length;
            if (pct != last_percent) {
                last_percent = pct;
                progress_cb(pct);
            }
        }
    }

    free(buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        esp_ota_abort(ota_handle);
        return err;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "OTA complete! Rebooting...");
    esp_restart();
    return ESP_OK; /* unreachable */
}

const char *ota_get_running_version(void)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    return desc ? desc->version : "unknown";
}
