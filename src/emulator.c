#include "emulator.h"
#include "config.h"
#include "audio.h"
#include "lcd_driver.h"
#include "button.h"
#include "nes_emu.h"
#include "gb_emu.h"
#include "perf_monitor.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <stdatomic.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

static const char *TAG = "emulator";

/* ---- ROM validation helpers ---- */

static bool validate_nes_rom(const uint8_t *data, size_t size)
{
    if (size < 16) {
        ESP_LOGE(TAG, "NES ROM too small (%d bytes)", (int)size);
        return false;
    }
    /* iNES header magic: "NES\x1A" */
    if (data[0] != 'N' || data[1] != 'E' || data[2] != 'S' || data[3] != 0x1A) {
        ESP_LOGE(TAG, "Invalid iNES magic header");
        return false;
    }
    /* Sanity: PRG must be at least 1 bank */
    if (data[4] == 0) {
        ESP_LOGE(TAG, "NES ROM has 0 PRG banks");
        return false;
    }
    size_t expected = 16 + (size_t)data[4] * 16384 + (size_t)data[5] * 8192;
    if (data[6] & 0x04) expected += 512; /* trainer */
    if (size < expected) {
        ESP_LOGE(TAG, "NES ROM truncated: need %d, have %d", (int)expected, (int)size);
        return false;
    }
    return true;
}

/* Nintendo logo bytes at offset 0x0104-0x0133 in GB ROM */
static const uint8_t gb_logo[48] = {
    0xCE,0xED,0x66,0x66,0xCC,0x0D,0x00,0x0B,0x03,0x73,0x00,0x83,0x00,0x0C,0x00,0x0D,
    0x00,0x08,0x11,0x1F,0x88,0x89,0x00,0x0E,0xDC,0xCC,0x6E,0xE6,0xDD,0xDD,0xD9,0x99,
    0xBB,0xBB,0x67,0x63,0x6E,0x0E,0xEC,0xCC,0xDD,0xDC,0x99,0x9F,0xBB,0xB9,0x33,0x3E,
};

static bool validate_gb_rom(const uint8_t *data, size_t size)
{
    if (size < 0x150) {
        ESP_LOGE(TAG, "GB ROM too small (%d bytes)", (int)size);
        return false;
    }
    /* Check Nintendo logo at 0x0104 */
    if (memcmp(data + 0x0104, gb_logo, sizeof(gb_logo)) != 0) {
        ESP_LOGE(TAG, "GB ROM: Nintendo logo mismatch");
        return false;
    }
    /* Header checksum at 0x014D */
    uint8_t cksum = 0;
    for (int i = 0x0134; i <= 0x014C; i++) {
        cksum = cksum - data[i] - 1;
    }
    if (cksum != data[0x014D]) {
        ESP_LOGE(TAG, "GB ROM: header checksum failed (expected 0x%02X, got 0x%02X)",
                 cksum, data[0x014D]);
        return false;
    }
    return true;
}

static volatile bool running = false;
static volatile bool paused = false;
static atomic_uint_fast16_t input_state = 0;
static emu_type_t current_type = EMU_TYPE_NONE;
static TaskHandle_t emu_task_handle = NULL;
static SemaphoreHandle_t stop_sem = NULL;  /* P3-4: clean stop synchronization */

/* LCD-sized output buffer for scaled display (single buffer) */
static uint16_t *lcd_framebuf = NULL;

/* Pre-computed scaling LUTs */
static uint16_t nes_scale_x[240];
static uint16_t nes_scale_y[225];
static uint16_t gb_scale_x[240];
static uint16_t gb_scale_y[216];
static bool scale_luts_ready = false;

static void init_scale_luts(void)
{
    if (scale_luts_ready) return;
    for (int x = 0; x < 240; x++) {
        nes_scale_x[x] = x * NES_WIDTH / 240;
        gb_scale_x[x]  = x * GB_WIDTH / 240;
    }
    for (int y = 0; y < 225; y++)
        nes_scale_y[y] = y * NES_HEIGHT / 225;
    for (int y = 0; y < 216; y++)
        gb_scale_y[y] = y * GB_HEIGHT / 216;
    scale_luts_ready = true;
}

emu_type_t emulator_detect_type(const char *rom_path)
{
    if (!rom_path) return EMU_TYPE_NONE;
    const char *ext = strrchr(rom_path, '.');
    if (!ext) return EMU_TYPE_NONE;
    if (strcasecmp(ext, ".nes") == 0) return EMU_TYPE_NES;
    if (strcasecmp(ext, ".gb") == 0)  return EMU_TYPE_GB;
    return EMU_TYPE_NONE;
}

/* ---- Load ROM file into PSRAM (hardened against SD card removal) ---- */
#define ROM_MAX_SIZE (2 * 1024 * 1024)  /* 2 MB sanity limit */

static uint8_t *load_rom_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open ROM: %s", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        ESP_LOGE(TAG, "Seek failed on ROM file");
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz <= 0 || (size_t)sz > ROM_MAX_SIZE) {
        ESP_LOGE(TAG, "ROM size invalid or too large: %ld", sz);
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    uint8_t *buf = heap_caps_malloc((size_t)sz, MALLOC_CAP_SPIRAM);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate %ld bytes for ROM", sz);
        fclose(f);
        return NULL;
    }

    size_t rd = fread(buf, 1, (size_t)sz, f);
    int ferr = ferror(f);
    fclose(f);

    if (ferr || rd != (size_t)sz) {
        ESP_LOGE(TAG, "ROM read incomplete: expected %ld, got %d (err=%d)",
                 sz, (int)rd, ferr);
        free(buf);
        return NULL;
    }
    *out_size = (size_t)sz;
    return buf;
}

/* ---- Scale NES 256×240 → LCD 240×320 (LUT-accelerated nearest-neighbour) ---- */
static void scale_nes_to_lcd(const uint16_t *src, uint16_t *dst)
{
    const int out_w = 240;
    const int out_h = 225;
    const int y_offset = (320 - out_h) / 2;

    memset(dst, 0, y_offset * out_w * 2);

    for (int y = 0; y < out_h; y++) {
        const uint16_t *srow = src + nes_scale_y[y] * NES_WIDTH;
        uint16_t *drow = dst + (y + y_offset) * out_w;
        for (int x = 0; x < out_w; x++) {
            drow[x] = srow[nes_scale_x[x]];
        }
    }

    memset(dst + (y_offset + out_h) * out_w, 0,
           (320 - y_offset - out_h) * out_w * 2);
}

/* ---- Scale GB 160×144 → LCD 240×320 (LUT-accelerated nearest-neighbour) ---- */
static void scale_gb_to_lcd(const uint16_t *src, uint16_t *dst)
{
    const int out_w = 240;
    const int out_h = 216;
    const int y_offset = (320 - out_h) / 2;

    memset(dst, 0, y_offset * out_w * 2);

    for (int y = 0; y < out_h; y++) {
        const uint16_t *srow = src + gb_scale_y[y] * GB_WIDTH;
        uint16_t *drow = dst + (y + y_offset) * out_w;
        for (int x = 0; x < out_w; x++) {
            drow[x] = srow[gb_scale_x[x]];
        }
    }

    memset(dst + (y_offset + out_h) * out_w, 0,
           (320 - y_offset - out_h) * out_w * 2);
}

/* ---- Full-frame flush (single buffer, no memcmp overhead) ---- */
static void flush_framebuf(const uint16_t *buf)
{
    /* Flush in 16-line bands to keep SPI DMA transfers manageable */
    const int w = LCD_WIDTH;
    const int h = LCD_HEIGHT;
    for (int band_y = 0; band_y < h; band_y += 16) {
        int band_end = band_y + 16;
        if (band_end > h) band_end = h;
        lcd_flush_area(0, band_y, w - 1, band_end - 1,
                       buf + band_y * w);
    }
}

/* ---- Map button bitmask to NES/GB joypad ---- */
static uint8_t map_buttons_nes(uint16_t state)
{
    uint8_t j = 0;
    if (state & (1 << BTN_ID_A))      j |= NES_BTN_A;
    if (state & (1 << BTN_ID_B))      j |= NES_BTN_B;
    if (state & (1 << BTN_ID_SELECT)) j |= NES_BTN_SELECT;
    if (state & (1 << BTN_ID_START))  j |= NES_BTN_START;
    if (state & (1 << BTN_ID_UP))     j |= NES_BTN_UP;
    if (state & (1 << BTN_ID_DOWN))   j |= NES_BTN_DOWN;
    if (state & (1 << BTN_ID_LEFT))   j |= NES_BTN_LEFT;
    if (state & (1 << BTN_ID_RIGHT))  j |= NES_BTN_RIGHT;
    return j;
}

static uint8_t map_buttons_gb(uint16_t state)
{
    uint8_t j = 0;
    if (state & (1 << BTN_ID_A))      j |= GB_BTN_A;
    if (state & (1 << BTN_ID_B))      j |= GB_BTN_B;
    if (state & (1 << BTN_ID_SELECT)) j |= GB_BTN_SELECT;
    if (state & (1 << BTN_ID_START))  j |= GB_BTN_START;
    if (state & (1 << BTN_ID_UP))     j |= GB_BTN_UP;
    if (state & (1 << BTN_ID_DOWN))   j |= GB_BTN_DOWN;
    if (state & (1 << BTN_ID_LEFT))   j |= GB_BTN_LEFT;
    if (state & (1 << BTN_ID_RIGHT))  j |= GB_BTN_RIGHT;
    return j;
}

/* ---- Frame-rate limiter helper ---- */
#define FRAME_US (1000000 / 60)

/* ---- NES Emulator Run ---- */
static void emu_nes_run(const char *rom_path)
{
    ESP_LOGI(TAG, "NES emulator starting: %s", rom_path);

    size_t rom_size = 0;
    uint8_t *rom_data = load_rom_file(rom_path, &rom_size);
    if (!rom_data) return;

    if (!validate_nes_rom(rom_data, rom_size)) {
        free(rom_data);
        return;
    }

    if (!nes_init(rom_data, rom_size)) {
        free(rom_data);
        return;
    }
    free(rom_data); /* Cart made its own copy */

    while (running) {
        if (paused) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        int64_t t_start = esp_timer_get_time();

        nes_set_joypad(map_buttons_nes(atomic_load(&input_state)));
        nes_run_frame();

        const uint16_t *fb = nes_get_framebuffer();
        scale_nes_to_lcd(fb, lcd_framebuf);
        perf_monitor_draw_overlay(lcd_framebuf, LCD_WIDTH, LCD_HEIGHT);
        perf_monitor_record_frame();
        flush_framebuf(lcd_framebuf);

        int audio_count = 0;
        const int16_t *audio = nes_get_audio(&audio_count);
        if (audio_count > 0) audio_write(audio, audio_count);

        int64_t elapsed = esp_timer_get_time() - t_start;
        if (elapsed < FRAME_US) {
            vTaskDelay(pdMS_TO_TICKS((FRAME_US - elapsed) / 1000));
        }
    }

    nes_shutdown();
    ESP_LOGI(TAG, "NES emulator stopped");
}

/* ---- GameBoy Emulator Run ---- */
static void emu_gb_run(const char *rom_path)
{
    ESP_LOGI(TAG, "GameBoy emulator starting: %s", rom_path);

    size_t rom_size = 0;
    uint8_t *rom_data = load_rom_file(rom_path, &rom_size);
    if (!rom_data) return;

    if (!validate_gb_rom(rom_data, rom_size)) {
        free(rom_data);
        return;
    }

    if (!gb_init(rom_data, rom_size)) {
        free(rom_data);
        return;
    }
    free(rom_data);

    while (running) {
        if (paused) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        int64_t t_start = esp_timer_get_time();

        gb_set_joypad(map_buttons_gb(atomic_load(&input_state)));
        gb_run_frame();

        const uint16_t *fb = gb_get_framebuffer();
        scale_gb_to_lcd(fb, lcd_framebuf);
        perf_monitor_draw_overlay(lcd_framebuf, LCD_WIDTH, LCD_HEIGHT);
        perf_monitor_record_frame();
        flush_framebuf(lcd_framebuf);

        int audio_count = 0;
        const int16_t *audio = gb_get_audio(&audio_count);
        if (audio_count > 0) audio_write(audio, audio_count);

        int64_t elapsed = esp_timer_get_time() - t_start;
        if (elapsed < FRAME_US) {
            vTaskDelay(pdMS_TO_TICKS((FRAME_US - elapsed) / 1000));
        }
    }

    gb_shutdown();
    ESP_LOGI(TAG, "GameBoy emulator stopped");
}

/* ---- Emulator Task (Core 1) ---- */
static void emulator_task(void *arg)
{
    const char *rom_path = (const char *)arg;

    switch (current_type) {
        case EMU_TYPE_NES: emu_nes_run(rom_path); break;
        case EMU_TYPE_GB:  emu_gb_run(rom_path);  break;
        default: ESP_LOGE(TAG, "Unknown emulator type"); break;
    }

    if (lcd_framebuf) {
        free(lcd_framebuf);
        lcd_framebuf = NULL;
    }

    running = false;
    emu_task_handle = NULL;

    /* Signal the stop semaphore so emulator_stop() doesn't spin */
    if (stop_sem) xSemaphoreGive(stop_sem);

    vTaskDelete(NULL);
}

static char rom_path_buf[300];

bool emulator_start(const char *rom_path)
{
    if (running) {
        ESP_LOGW(TAG, "Emulator already running");
        return false;
    }

    current_type = emulator_detect_type(rom_path);
    if (current_type == EMU_TYPE_NONE) {
        ESP_LOGE(TAG, "Unsupported ROM type: %s", rom_path);
        return false;
    }

    strncpy(rom_path_buf, rom_path, sizeof(rom_path_buf) - 1);
    rom_path_buf[sizeof(rom_path_buf) - 1] = '\0';

    init_scale_luts();

    /* Create stop semaphore for clean shutdown (P3-4) */
    if (!stop_sem) {
        stop_sem = xSemaphoreCreateBinary();
    }

    size_t fb_size = LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t);
    lcd_framebuf = heap_caps_malloc(fb_size, MALLOC_CAP_SPIRAM);
    if (!lcd_framebuf) {
        ESP_LOGE(TAG, "Failed to allocate LCD framebuffer");
        return false;
    }
    memset(lcd_framebuf, 0, fb_size);
    paused = false;

    running = true;

    BaseType_t ret = xTaskCreatePinnedToCore(
        emulator_task, "emu_core", 32768,
        rom_path_buf, 6, &emu_task_handle, 1);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create emulator task");
        running = false;
        free(lcd_framebuf);
        lcd_framebuf = NULL;
        return false;
    }

    ESP_LOGI(TAG, "Emulator started on Core 1 (type=%d)", current_type);
    return true;
}

void emulator_stop(void)
{
    if (!running) return;
    running = false;
    ESP_LOGI(TAG, "Stopping emulator...");

    /* P3-4: Wait on semaphore instead of polling with timeout */
    if (stop_sem) {
        if (xSemaphoreTake(stop_sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
            ESP_LOGW(TAG, "Emulator task did not exit within 5s");
        }
    }

    /* Ensure cleanup even if task hung */
    if (lcd_framebuf) {
        free(lcd_framebuf);
        lcd_framebuf = NULL;
    }
    emu_task_handle = NULL;
    current_type = EMU_TYPE_NONE;
    ESP_LOGI(TAG, "Emulator stopped");
}

bool emulator_is_running(void)
{
    return running;
}

bool emulator_is_paused(void)
{
    return paused;
}

void emulator_pause(void)
{
    if (running && !paused) {
        paused = true;
        ESP_LOGI(TAG, "Emulator paused");
    }
}

void emulator_resume(void)
{
    if (running && paused) {
        paused = false;
        ESP_LOGI(TAG, "Emulator resumed");
    }
}

void emulator_set_input(uint16_t button_mask)
{
    atomic_store(&input_state, button_mask);
}

const char *emulator_get_rom_path(void)
{
    return rom_path_buf;
}

emu_type_t emulator_get_type(void)
{
    return current_type;
}

/* ---- P3-2: Save state system ---- */

/* Build save file path: /sdcard/saves/<basename>.sav */
static bool build_save_path(char *out, size_t out_size)
{
    const char *slash = strrchr(rom_path_buf, '/');
    const char *name = slash ? slash + 1 : rom_path_buf;
    if (name[0] == '\0') return false;

    /* Ensure saves directory exists */
    mkdir(SAVE_STATE_PATH, 0775);

    char base[128];
    strncpy(base, name, sizeof(base) - 1);
    base[sizeof(base) - 1] = '\0';

    /* Replace extension with .sav */
    char *dot = strrchr(base, '.');
    if (dot) *dot = '\0';

    int n = snprintf(out, out_size, "%s/%s.sav", SAVE_STATE_PATH, base);
    return n > 0 && (size_t)n < out_size;
}

/* Save state header for integrity checking */
#define SAVE_MAGIC 0x45535653  /* "ESAV" */
typedef struct {
    uint32_t magic;
    uint32_t emu_type;
    uint32_t data_size;
    uint32_t checksum;
} save_header_t;

static uint32_t calc_checksum(const uint8_t *data, size_t len)
{
    uint32_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc = (crc << 1) ^ data[i];
    }
    return crc;
}

bool emulator_save_state(void)
{
    if (!running || !paused) {
        ESP_LOGE(TAG, "Cannot save: emulator not paused");
        return false;
    }

    char path[256];
    if (!build_save_path(path, sizeof(path))) {
        ESP_LOGE(TAG, "Failed to build save path");
        return false;
    }

    /* Capture current framebuffer as the save state visual snapshot,
     * plus emulator RAM state. Since the emu core doesn't expose a
     * serializable state blob, we save the current LCD framebuffer
     * so we can restore the visual at least. For a full state save,
     * we would need emu core serialization (future work). */
    size_t fb_size = LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t);
    if (!lcd_framebuf) {
        ESP_LOGE(TAG, "No framebuffer to save");
        return false;
    }

    save_header_t hdr = {
        .magic = SAVE_MAGIC,
        .emu_type = (uint32_t)current_type,
        .data_size = (uint32_t)fb_size,
        .checksum = calc_checksum((const uint8_t *)lcd_framebuf, fb_size),
    };

    /* Write atomically: write to .tmp then rename */
    char tmp_path[260];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot create save file: %s", tmp_path);
        return false;
    }

    bool ok = true;
    if (fwrite(&hdr, sizeof(hdr), 1, f) != 1) ok = false;
    if (ok && fwrite(lcd_framebuf, 1, fb_size, f) != fb_size) ok = false;

    if (fclose(f) != 0) ok = false;

    if (!ok) {
        ESP_LOGE(TAG, "Save write failed");
        remove(tmp_path);
        return false;
    }

    /* Atomic rename */
    remove(path);
    if (rename(tmp_path, path) != 0) {
        ESP_LOGE(TAG, "Save rename failed");
        return false;
    }

    ESP_LOGI(TAG, "State saved: %s (%u bytes)", path, (unsigned)fb_size);
    return true;
}

bool emulator_load_state(void)
{
    if (!running || !paused) {
        ESP_LOGE(TAG, "Cannot load: emulator not paused");
        return false;
    }

    char path[256];
    if (!build_save_path(path, sizeof(path))) return false;

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "No save file: %s", path);
        return false;
    }

    save_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) {
        ESP_LOGE(TAG, "Failed to read save header");
        fclose(f);
        return false;
    }

    if (hdr.magic != SAVE_MAGIC) {
        ESP_LOGE(TAG, "Invalid save magic: 0x%08lX", (unsigned long)hdr.magic);
        fclose(f);
        return false;
    }

    if ((emu_type_t)hdr.emu_type != current_type) {
        ESP_LOGE(TAG, "Save type mismatch (save=%lu, current=%d)",
                 (unsigned long)hdr.emu_type, current_type);
        fclose(f);
        return false;
    }

    size_t fb_size = LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t);
    if (hdr.data_size != (uint32_t)fb_size) {
        ESP_LOGE(TAG, "Save data size mismatch");
        fclose(f);
        return false;
    }

    if (!lcd_framebuf) {
        fclose(f);
        return false;
    }

    if (fread(lcd_framebuf, 1, fb_size, f) != fb_size) {
        ESP_LOGE(TAG, "Failed to read save data");
        fclose(f);
        return false;
    }
    fclose(f);

    /* Verify checksum */
    uint32_t cksum = calc_checksum((const uint8_t *)lcd_framebuf, fb_size);
    if (cksum != hdr.checksum) {
        ESP_LOGE(TAG, "Save checksum mismatch");
        return false;
    }

    /* Flush restored framebuffer to display */
    flush_framebuf(lcd_framebuf);

    ESP_LOGI(TAG, "State loaded: %s", path);
    return true;
}

bool emulator_has_save_state(void)
{
    char path[256];
    if (!build_save_path(path, sizeof(path))) return false;

    struct stat st;
    return stat(path, &st) == 0 && st.st_size > (long)sizeof(save_header_t);
}
