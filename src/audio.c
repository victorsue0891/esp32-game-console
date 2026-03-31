#include "audio.h"
#include "config.h"

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include <stdatomic.h>
#include <string.h>

static const char *TAG = "audio";

static i2s_chan_handle_t tx_handle = NULL;
static int current_volume = VOLUME_DEFAULT;
static bool is_muted = false;

/* A/V sync credit: tracks I2S queue health.
 * Incremented on successful write (audio ahead), decremented on timeout (audio behind).
 * Emulator uses this to pace video: if credit > 2, audio is running fast, slow video slightly. */
static atomic_int av_credit = 2;

/* Fixed-point volume multiplier: vol * 256 / 100 */
static uint16_t vol_shift = 0;

static inline void update_vol_shift(void)
{
    vol_shift = (uint16_t)((current_volume * 256 + 50) / 100);
}

esp_err_t audio_init(void)
{
    ESP_LOGI(TAG, "Initializing I2S audio (MAX98357)...");

    /* Channel config – larger DMA buffers reduce underrun risk */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 24;   /* doubled: reduces underrun probability */
    chan_cfg.dma_frame_num = 512;

    esp_err_t ret = i2s_new_channel(&chan_cfg, &tx_handle, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Standard mode config */
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(I2S_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                         I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_PIN_BCK,
            .ws   = I2S_PIN_LRCK,
            .dout = I2S_PIN_DOUT,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(tx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2s_channel_enable(tx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    update_vol_shift();
    ESP_LOGI(TAG, "I2S audio initialized (sample rate: %d Hz)", I2S_SAMPLE_RATE);
    return ESP_OK;
}

void audio_write(const int16_t *samples, size_t count)
{
    if (!tx_handle || is_muted || count == 0 || !samples) return;

    /* Apply volume scaling with fixed-point multiply (>>8 instead of /100) */
    int16_t buf[512];
    size_t remaining = count;
    const int16_t *src = samples;
    const uint16_t vs = vol_shift;

    while (remaining > 0) {
        size_t chunk = (remaining > 512) ? 512 : remaining;

        for (size_t i = 0; i < chunk; i++) {
            buf[i] = (int16_t)(((int32_t)src[i] * vs) >> 8);
        }

        size_t bytes_written = 0;
        size_t bytes_to_write = chunk * sizeof(int16_t);
        i2s_channel_write(tx_handle, buf, bytes_to_write,
                          &bytes_written, pdMS_TO_TICKS(10));

        /* Track I2S queue health for A/V sync credit. */
        if (bytes_written >= bytes_to_write) {
            /* Audio queue has space — credit the video side. */
            int c = atomic_load(&av_credit);
            if (c < 4) atomic_fetch_add(&av_credit, 1);
        } else {
            /* Write timed out — audio is falling behind. */
            int c = atomic_load(&av_credit);
            if (c > 0) atomic_fetch_sub(&av_credit, 1);

            /* Fill remaining space with silence so the I2S clock keeps
             * running and avoids audible pops or DAC underrun. */
            static const int16_t silence[512] = {0};
            size_t gap = bytes_to_write - bytes_written;
            while (gap > 0) {
                size_t sil_len = (gap > sizeof(silence)) ? sizeof(silence) : gap;
                size_t sil_written = 0;
                i2s_channel_write(tx_handle, silence, sil_len, &sil_written, 0);
                if (sil_written == 0) break;
                gap -= sil_written;
            }
        }

        src += chunk;
        remaining -= chunk;
    }
}

int audio_get_av_credit(void)
{
    return atomic_load(&av_credit);
}

void audio_reset_av_credit(void)
{
    atomic_store(&av_credit, 2);
}

void audio_set_volume(int vol)
{
    if (vol < VOLUME_MIN) vol = VOLUME_MIN;
    if (vol > VOLUME_MAX) vol = VOLUME_MAX;
    current_volume = vol;
    update_vol_shift();
    ESP_LOGI(TAG, "Volume set to %d%%", current_volume);
}

int audio_get_volume(void)
{
    return current_volume;
}

void audio_set_mute(bool mute)
{
    is_muted = mute;
    ESP_LOGI(TAG, "Audio %s", mute ? "muted" : "unmuted");
}

void audio_deinit(void)
{
    if (tx_handle) {
        i2s_channel_disable(tx_handle);
        i2s_del_channel(tx_handle);
        tx_handle = NULL;
        ESP_LOGI(TAG, "Audio de-initialized");
    }
}
