#include "theme_manager.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "theme";
static const char *NVS_NAMESPACE = "system";
static const char *NVS_KEY_THEME = "theme";

static theme_id_t current_theme = THEME_DARK;

/* ---- Built-in themes ---- */

static const theme_colors_t themes[THEME_COUNT] = {
    [THEME_DARK] = {
        .bg_primary    = {.full = 0x1082},  /* #1A1A2E → RGB565 */
        .bg_secondary  = {.full = 0x1088},  /* #202040 */
        .text_primary  = {.full = 0xFFFF},  /* white */
        .text_secondary= {.full = 0x8410},  /* #808080 */
        .accent        = {.full = 0x0664},  /* #00CC66 */
        .list_bg       = {.full = 0x1088},  /* #202040 */
        .list_focus    = {.full = 0x180C},  /* #303060 */
    },
    [THEME_LIGHT] = {
        .bg_primary    = {.full = 0xE71C},  /* #F0F0F0 */
        .bg_secondary  = {.full = 0xFFFF},  /* #FFFFFF */
        .text_primary  = {.full = 0x2104},  /* #222222 */
        .text_secondary= {.full = 0x632C},  /* #666666 */
        .accent        = {.full = 0x03B9},  /* #0077CC */
        .list_bg       = {.full = 0xFFFF},  /* #FFFFFF */
        .list_focus    = {.full = 0xD6BF},  /* #D0E0FF */
    },
    [THEME_RETRO] = {
        .bg_primary    = {.full = 0x09C1},  /* #0F380F */
        .bg_secondary  = {.full = 0x3186},  /* #306230 */
        .text_primary  = {.full = 0x9DE1},  /* #9BBC0F */
        .text_secondary= {.full = 0x8D61},  /* #8BAC0F */
        .accent        = {.full = 0x9DE1},  /* #9BBC0F */
        .list_bg       = {.full = 0x3186},  /* #306230 */
        .list_focus    = {.full = 0x09C1},  /* #0F380F */
    },
};

static const char *theme_names[THEME_COUNT] = {
    [THEME_DARK]  = "Dark",
    [THEME_LIGHT] = "Light",
    [THEME_RETRO] = "Retro",
};

void theme_manager_init(void)
{
    nvs_handle_t hnd;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &hnd) == ESP_OK) {
        uint8_t val = 0;
        if (nvs_get_u8(hnd, NVS_KEY_THEME, &val) == ESP_OK && val < THEME_COUNT) {
            current_theme = (theme_id_t)val;
        }
        nvs_close(hnd);
    }
    ESP_LOGI(TAG, "Theme loaded: %s", theme_names[current_theme]);
}

void theme_manager_set(theme_id_t id)
{
    if (id >= THEME_COUNT) return;
    current_theme = id;

    nvs_handle_t hnd;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &hnd) == ESP_OK) {
        nvs_set_u8(hnd, NVS_KEY_THEME, (uint8_t)id);
        nvs_commit(hnd);
        nvs_close(hnd);
    }
    ESP_LOGI(TAG, "Theme set: %s", theme_names[id]);
}

theme_id_t theme_manager_get(void)
{
    return current_theme;
}

const theme_colors_t *theme_manager_colors(void)
{
    return &themes[current_theme];
}

const char *theme_manager_name(theme_id_t id)
{
    if (id >= THEME_COUNT) return "?";
    return theme_names[id];
}
