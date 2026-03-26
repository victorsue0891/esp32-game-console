#include "battery.h"
#include "config.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

#include <stdbool.h>

static const char *TAG = "battery";

static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t cali_handle = NULL;
static bool cali_valid = false;
static bool paused_flag = false;

/* Cached latest readings */
static int last_voltage_mv = BAT_FULL_MV;
static int last_level = 100;

esp_err_t battery_init(void)
{
    ESP_LOGI(TAG, "Initializing battery ADC (GPIO%d, ADC2_CH%d)...",
             BAT_ADC_GPIO, BAT_ADC_CHANNEL);

    /* Create ADC2 oneshot unit */
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = BAT_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t ret = adc_oneshot_new_unit(&unit_cfg, &adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Configure the channel */
    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ret = adc_oneshot_config_channel(adc_handle, BAT_ADC_CHANNEL, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Calibration (curve fitting on ESP32-S3) */
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = BAT_ADC_UNIT,
        .chan = BAT_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle) == ESP_OK) {
        cali_valid = true;
        ESP_LOGI(TAG, "ADC calibration (curve fitting) OK");
    }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = BAT_ADC_UNIT,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_line_fitting(&cali_cfg, &cali_handle) == ESP_OK) {
        cali_valid = true;
        ESP_LOGI(TAG, "ADC calibration (line fitting) OK");
    }
#endif
    if (!cali_valid) {
        ESP_LOGW(TAG, "ADC calibration not available; using raw conversion");
    }

    /* Take initial reading */
    int raw = 0;
    if (adc_oneshot_read(adc_handle, BAT_ADC_CHANNEL, &raw) == ESP_OK) {
        if (cali_valid) {
            int mv = 0;
            adc_cali_raw_to_voltage(cali_handle, raw, &mv);
            last_voltage_mv = (int)(mv * BAT_VDIV_RATIO);
        } else {
            /* Manual: ADC_ATTEN_DB_11 ≈ 0-2500 mV, 12-bit */
            last_voltage_mv = (int)(raw * 2500 / 4095 * BAT_VDIV_RATIO);
        }
        /* Clamp and convert to percentage */
        if (last_voltage_mv > BAT_FULL_MV) last_voltage_mv = BAT_FULL_MV;
        if (last_voltage_mv < BAT_EMPTY_MV) last_voltage_mv = BAT_EMPTY_MV;
        last_level = (last_voltage_mv - BAT_EMPTY_MV) * 100 / (BAT_FULL_MV - BAT_EMPTY_MV);
    }

    ESP_LOGI(TAG, "Battery init: %d mV (%d%%)", last_voltage_mv, last_level);
    return ESP_OK;
}

int battery_get_level(void)
{
    if (!adc_handle || paused_flag) return last_level;

    int raw = 0;
    esp_err_t ret = adc_oneshot_read(adc_handle, BAT_ADC_CHANNEL, &raw);
    if (ret != ESP_OK) {
        /* ADC2 busy (WiFi) — return cached value */
        return last_level;
    }

    int mv;
    if (cali_valid) {
        adc_cali_raw_to_voltage(cali_handle, raw, &mv);
        mv = (int)(mv * BAT_VDIV_RATIO);
    } else {
        mv = (int)(raw * 2500 / 4095 * BAT_VDIV_RATIO);
    }

    if (mv > BAT_FULL_MV) mv = BAT_FULL_MV;
    if (mv < BAT_EMPTY_MV) mv = BAT_EMPTY_MV;
    last_voltage_mv = mv;
    last_level = (mv - BAT_EMPTY_MV) * 100 / (BAT_FULL_MV - BAT_EMPTY_MV);
    return last_level;
}

int battery_get_voltage_mv(void)
{
    battery_get_level(); /* refresh */
    return last_voltage_mv;
}

void battery_pause(void)
{
    paused_flag = true;
    ESP_LOGI(TAG, "Battery ADC paused (WiFi active)");
}

void battery_resume(void)
{
    paused_flag = false;
    ESP_LOGI(TAG, "Battery ADC resumed");
}

void battery_deinit(void)
{
    if (cali_handle) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_delete_scheme_curve_fitting(cali_handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_delete_scheme_line_fitting(cali_handle);
#endif
        cali_handle = NULL;
        cali_valid = false;
    }
    if (adc_handle) {
        adc_oneshot_del_unit(adc_handle);
        adc_handle = NULL;
    }
    ESP_LOGI(TAG, "Battery de-initialized");
}
