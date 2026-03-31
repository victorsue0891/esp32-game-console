#include "emulator.h"
#include "emulator_driver.h"
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
#include "freertos/queue.h"
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
    if (data[0] != 'N' || data[1] != 'E' || data[2] != 'S' || data[3] != 0x1A) {
        ESP_LOGE(TAG, "Invalid iNES magic header");
        return false;
    }
    if (data[4] == 0) {
        ESP_LOGE(TAG, "NES ROM has 0 PRG banks");
        return false;
    }
    size_t expected = 16 + (size_t)data[4] * 16384 + (size_t)data[5] * 8192;
    if (data[6] & 0x04) expected += 512;
    if (size < expected) {
        ESP_LOGE(TAG, "NES ROM truncated: need %d, have %d", (int)expected, (int)size);
        return false;
    }
    return true;
}

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
    if (memcmp(data + 0x0104, gb_logo, sizeof(gb_logo)) != 0) {
        ESP_LOGE(TAG, "GB ROM: Nintendo logo mismatch");
        return false;
    }
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

/* ---- Button mapping helpers ---- */

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

/* ---- emulator_driver_t wrappers and vtable instances ---- */

static void nes_drv_set_input(uint16_t buttons)
{
    nes_set_joypad(map_buttons_nes(buttons));
}

static const emulator_driver_t nes_driver = {
    .name            = "NES",
    .ext             = ".nes",
    .fb_width        = NES_WIDTH,
    .fb_height       = NES_HEIGHT,
    .validate        = validate_nes_rom,
    .init            = nes_init,
    .run_frame       = nes_run_frame,
    .set_input       = nes_drv_set_input,
    .get_framebuffer = nes_get_framebuffer,
    .get_audio       = nes_get_audio,
    .get_state_size  = nes_get_state_size,
    .save_state      = nes_save_state,
    .load_state      = nes_load_state,
    .shutdown        = nes_shutdown,
};

static void gb_drv_set_input(uint16_t buttons)
{
    gb_set_joypad(map_buttons_gb(buttons));
}

static const emulator_driver_t gb_driver = {
    .name            = "GB",
    .ext             = ".gb",
    .fb_width        = GB_WIDTH,
    .fb_height       = GB_HEIGHT,
    .validate        = validate_gb_rom,
    .init            = gb_init,
    .run_frame       = gb_run_frame,
    .set_input       = gb_drv_set_input,
    .get_framebuffer = gb_get_framebuffer,
    .get_audio       = gb_get_audio,
    .get_state_size  = gb_get_state_size,
    .save_state      = gb_save_state,
    .load_state      = gb_load_state,
    .shutdown        = gb_shutdown,
};

/* Null-terminated driver registry — add new systems here only */
static const emulator_driver_t *const drivers[] = {
    &nes_driver,
    &gb_driver,
    NULL,
};

static const emulator_driver_t *find_driver_by_ext(const char *ext)
{
    for (int i = 0; drivers[i]; i++) {
        if (strcasecmp(ext, drivers[i]->ext) == 0) return drivers[i];
    }
    return NULL;
}

static emu_type_t driver_to_type(const emulator_driver_t *drv)
{
    if (drv == &nes_driver) return EMU_TYPE_NES;
    if (drv == &gb_driver)  return EMU_TYPE_GB;
    return EMU_TYPE_NONE;
}

/* ---- Runtime state ---- */

static volatile bool running = false;
static volatile bool paused  = false;
static atomic_uint_fast16_t input_state = 0;
static const emulator_driver_t *current_driver = NULL;
static TaskHandle_t emu_task_handle = NULL;
static SemaphoreHandle_t stop_sem = NULL;

/* ---- Double framebuffer + dedicated LCD flush task ----
 *
 * lcd_fb[2]   — two PSRAM buffers (LCD_WIDTH × LCD_HEIGHT × 2 bytes each)
 * write_idx   — index of the buffer the emulator writes to next
 * disp_queue  — queue of size 1: emulator posts the index of the just-finished frame
 * buf_free_sem — counting semaphore (initial count 2) tracking write slots.
 *               Emulator takes one slot before writing; LCD task returns it after flush.
 *
 * Safety invariant: because buf_free_sem starts at 2 and the queue holds at most 1
 * item, at most 1 buffer is being flushed (dequeued by LCD) and at most 1 is queued
 * for display at any time.  The emulator's write_idx always points to the buffer NOT
 * currently owned by the LCD task, so there is never a concurrent read/write.
 */
static uint16_t *lcd_fb[2]  = {NULL, NULL};
static volatile int write_idx = 0;
static QueueHandle_t     disp_queue   = NULL;
static SemaphoreHandle_t buf_free_sem = NULL;
static TaskHandle_t      lcd_flush_task_handle = NULL;

/* ---- Generic scale LUTs (built once per driver) ---- */
static uint16_t scale_lut_x[LCD_WIDTH];   /* src X for each output column */
static uint16_t scale_lut_y[LCD_HEIGHT];  /* src Y for each output row     */
static int      lut_out_h  = 0;           /* letterboxed output height      */
static int      lut_fb_width = 0;         /* driver fb_width used to build LUTs */

static void init_scale_luts(const emulator_driver_t *drv)
{
    if (lut_fb_width == drv->fb_width) return;  /* already built for this driver */

    /* Scale to full LCD width; maintain aspect ratio; letterbox vertically */
    int out_h = (int)((long)LCD_WIDTH * drv->fb_height / drv->fb_width);
    if (out_h > LCD_HEIGHT) out_h = LCD_HEIGHT;

    for (int x = 0; x < LCD_WIDTH; x++)
        scale_lut_x[x] = (uint16_t)((long)x * drv->fb_width / LCD_WIDTH);
    for (int y = 0; y < out_h; y++)
        scale_lut_y[y] = (uint16_t)((long)y * drv->fb_height / out_h);

    lut_out_h   = out_h;
    lut_fb_width = drv->fb_width;
}

/* ---- Framebuffer scaling (generic, works for any driver) ---- */
static void scale_to_lcd(const uint16_t *src, uint16_t *dst)
{
    int out_h    = lut_out_h;
    int y_offset = (LCD_HEIGHT - out_h) / 2;

    if (y_offset > 0)
        memset(dst, 0, (size_t)y_offset * LCD_WIDTH * 2);

    for (int y = 0; y < out_h; y++) {
        const uint16_t *srow = src + scale_lut_y[y] * lut_fb_width;
        uint16_t *drow = dst + (y + y_offset) * LCD_WIDTH;
        for (int x = 0; x < LCD_WIDTH; x++) {
            drow[x] = srow[scale_lut_x[x]];
        }
    }

    int fill_start = (y_offset + out_h) * LCD_WIDTH;
    int fill_count = LCD_WIDTH * LCD_HEIGHT - fill_start;
    if (fill_count > 0)
        memset(dst + fill_start, 0, (size_t)fill_count * 2);
}

/* ---- Full-frame LCD flush (16-line band SPI DMA) ---- */
static void flush_framebuf(const uint16_t *buf)
{
    for (int band_y = 0; band_y < LCD_HEIGHT; band_y += 16) {
        int band_end = band_y + 16;
        if (band_end > LCD_HEIGHT) band_end = LCD_HEIGHT;
        lcd_flush_area(0, band_y, LCD_WIDTH - 1, band_end - 1,
                       buf + band_y * LCD_WIDTH);
    }
}

/* ---- Dedicated LCD flush task (Core 1, priority 5) ----
 *
 * Runs at one priority below the emulator task (prio 6), so the emulator
 * can preempt it at any scheduling point.  While the SPI DMA is in flight
 * (spi_device_transmit yields), the emulator task runs and computes the
 * next frame into the OTHER framebuffer — genuine pipeline overlap.
 */
static void lcd_flush_task(void *arg)
{
    while (true) {
        uint8_t ridx;
        /* 500 ms timeout lets the task notice when the emulator has stopped */
        if (xQueueReceive(disp_queue, &ridx, pdMS_TO_TICKS(500)) == pdTRUE) {
            flush_framebuf(lcd_fb[ridx]);
            xSemaphoreGive(buf_free_sem);  /* return write slot to emulator */
        }
    }
}

/* ---- ROM loader ---- */
#define ROM_MAX_SIZE (2 * 1024 * 1024)

static uint8_t *load_rom_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) { ESP_LOGE(TAG, "Cannot open ROM: %s", path); return NULL; }

    if (fseek(f, 0, SEEK_END) != 0) {
        ESP_LOGE(TAG, "Seek failed"); fclose(f); return NULL;
    }
    long sz = ftell(f);
    if (sz <= 0 || (size_t)sz > ROM_MAX_SIZE) {
        ESP_LOGE(TAG, "ROM size invalid: %ld", sz); fclose(f); return NULL;
    }
    fseek(f, 0, SEEK_SET);

    uint8_t *buf = heap_caps_malloc((size_t)sz, MALLOC_CAP_SPIRAM);
    if (!buf) {
        ESP_LOGE(TAG, "PSRAM alloc failed for ROM (%ld bytes)", sz);
        fclose(f); return NULL;
    }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    int ferr = ferror(f);
    fclose(f);

    if (ferr || rd != (size_t)sz) {
        ESP_LOGE(TAG, "ROM read incomplete (%d/%ld)", (int)rd, sz);
        free(buf); return NULL;
    }
    *out_size = (size_t)sz;
    return buf;
}

/* ---- Unified emulator task (Core 1, priority 6) ---- */
#define FRAME_US (1000000 / 60)

static void emulator_task(void *arg)
{
    const char *rom_path = (const char *)arg;

    /* Load ROM */
    size_t rom_size = 0;
    uint8_t *rom_data = load_rom_file(rom_path, &rom_size);
    if (!rom_data) goto task_exit;

    if (current_driver->validate && !current_driver->validate(rom_data, rom_size)) {
        free(rom_data); goto task_exit;
    }

    if (!current_driver->init(rom_data, rom_size)) {
        free(rom_data); goto task_exit;
    }
    free(rom_data);

    ESP_LOGI(TAG, "[%s] emulator started", current_driver->name);

    while (running) {
        if (paused) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        int64_t t_start = esp_timer_get_time();

        /* Run one frame of emulation (pure CPU — no blocking calls) */
        current_driver->set_input((uint16_t)atomic_load(&input_state));
        current_driver->run_frame();

        /* --- Display pipeline ---
         * Try to claim a write slot (non-blocking).  If both slots are
         * occupied the LCD task is still flushing; we skip display for
         * this frame but continue emulation and audio at full speed.
         */
        if (xSemaphoreTake(buf_free_sem, 0) == pdTRUE) {
            int widx = write_idx;
            scale_to_lcd(current_driver->get_framebuffer(), lcd_fb[widx]);
            perf_monitor_draw_overlay(lcd_fb[widx], LCD_WIDTH, LCD_HEIGHT);

            uint8_t send_idx = (uint8_t)widx;
            if (xQueueSend(disp_queue, &send_idx, 0) == pdTRUE) {
                /* Swap: next frame goes to the other buffer */
                write_idx = widx ^ 1;
            } else {
                /* LCD queue full — return the slot, keep write_idx unchanged */
                xSemaphoreGive(buf_free_sem);
            }
        }

        perf_monitor_record_frame();

        /* Audio */
        int audio_count = 0;
        const int16_t *audio = current_driver->get_audio(&audio_count);
        if (audio_count > 0) audio_write(audio, audio_count);

        /* A/V sync: if audio buffer is running well ahead, yield 2 ms so the
         * LCD flush task can catch up and prevent audio/video drift. */
        if (audio_get_av_credit() > 2) {
            vTaskDelay(pdMS_TO_TICKS(2));
        }

        /* Frame-rate limiter: target 60 fps */
        int64_t elapsed = esp_timer_get_time() - t_start;
        if (elapsed < FRAME_US) {
            vTaskDelay(pdMS_TO_TICKS((FRAME_US - elapsed) / 1000));
        }
        /* The vTaskDelay above yields CPU so the LCD flush task (prio 5)
         * can run and transmit the pending frame via SPI DMA. */
    }

    current_driver->shutdown();
    ESP_LOGI(TAG, "[%s] emulator stopped", current_driver->name);

task_exit:
    /* Do NOT free lcd_fb[] here — the LCD flush task may still be reading it.
     * emulator_stop() kills the LCD task first, then frees lcd_fb[]. */
    running = false;
    emu_task_handle = NULL;
    if (stop_sem) xSemaphoreGive(stop_sem);
    vTaskDelete(NULL);
}

/* ---- Public API ---- */

static char rom_path_buf[300];

emu_type_t emulator_detect_type(const char *rom_path)
{
    if (!rom_path) return EMU_TYPE_NONE;
    const char *ext = strrchr(rom_path, '.');
    if (!ext) return EMU_TYPE_NONE;
    const emulator_driver_t *drv = find_driver_by_ext(ext);
    return driver_to_type(drv);
}

bool emulator_start(const char *rom_path)
{
    if (running) {
        ESP_LOGW(TAG, "Emulator already running");
        return false;
    }

    const char *ext = strrchr(rom_path, '.');
    if (!ext) { ESP_LOGE(TAG, "No file extension: %s", rom_path); return false; }

    current_driver = find_driver_by_ext(ext);
    if (!current_driver) {
        ESP_LOGE(TAG, "Unsupported ROM type: %s", rom_path);
        return false;
    }

    strncpy(rom_path_buf, rom_path, sizeof(rom_path_buf) - 1);
    rom_path_buf[sizeof(rom_path_buf) - 1] = '\0';

    init_scale_luts(current_driver);

    /* Allocate double framebuffers in PSRAM */
    size_t fb_size = LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t);
    for (int i = 0; i < 2; i++) {
        lcd_fb[i] = heap_caps_malloc(fb_size, MALLOC_CAP_SPIRAM);
        if (!lcd_fb[i]) {
            ESP_LOGE(TAG, "Failed to allocate lcd_fb[%d]", i);
            for (int j = 0; j < i; j++) { free(lcd_fb[j]); lcd_fb[j] = NULL; }
            return false;
        }
        memset(lcd_fb[i], 0, fb_size);
    }
    write_idx = 0;

    /* Display synchronisation primitives */
    if (!buf_free_sem) {
        buf_free_sem = xSemaphoreCreateCounting(2, 2);
    } else {
        /* Reset to full count in case of restart */
        xSemaphoreGive(buf_free_sem);
        xSemaphoreGive(buf_free_sem);
        /* Drain any leftover count beyond 2 */
        while (xSemaphoreTake(buf_free_sem, 0) == pdTRUE) {
            /* drain */ ;
        }
        xSemaphoreGive(buf_free_sem);
        xSemaphoreGive(buf_free_sem);
    }

    if (!disp_queue) {
        disp_queue = xQueueCreate(1, sizeof(uint8_t));
    } else {
        xQueueReset(disp_queue);
    }

    /* LCD flush task */
    xTaskCreatePinnedToCore(
        lcd_flush_task, "lcd_flush", 4096,
        NULL, 5, &lcd_flush_task_handle, 1);

    /* Emulator stop semaphore */
    if (!stop_sem) stop_sem = xSemaphoreCreateBinary();

    audio_reset_av_credit();  /* reset stale credit from any previous session */
    paused = false;
    running = true;

    BaseType_t ret = xTaskCreatePinnedToCore(
        emulator_task, "emu_core", 32768,
        rom_path_buf, 6, &emu_task_handle, 1);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create emulator task");
        running = false;
        vTaskDelete(lcd_flush_task_handle);
        lcd_flush_task_handle = NULL;
        for (int i = 0; i < 2; i++) { free(lcd_fb[i]); lcd_fb[i] = NULL; }
        return false;
    }

    ESP_LOGI(TAG, "Emulator started: %s (%s)", current_driver->name, rom_path);
    return true;
}

void emulator_stop(void)
{
    if (!running) return;
    running = false;
    ESP_LOGI(TAG, "Stopping emulator...");

    if (stop_sem) {
        if (xSemaphoreTake(stop_sem, pdMS_TO_TICKS(5000)) != pdTRUE)
            ESP_LOGW(TAG, "Emulator task did not exit within 5 s");
    }

    /* Kill the LCD flush task first, THEN free lcd_fb[].
     * This ordering is mandatory: freeing before kill creates a use-after-free
     * if the LCD task is mid-flush when the emulator task signals stop_sem. */
    if (lcd_flush_task_handle) {
        vTaskDelete(lcd_flush_task_handle);
        lcd_flush_task_handle = NULL;
    }
    for (int i = 0; i < 2; i++) {
        if (lcd_fb[i]) { free(lcd_fb[i]); lcd_fb[i] = NULL; }
    }

    emu_task_handle = NULL;
    current_driver = NULL;
    ESP_LOGI(TAG, "Emulator stopped");
}

bool emulator_is_running(void)  { return running; }
bool emulator_is_paused(void)   { return paused; }

void emulator_pause(void)
{
    if (running && !paused) { paused = true; ESP_LOGI(TAG, "Paused"); }
}

void emulator_resume(void)
{
    if (running && paused) { paused = false; ESP_LOGI(TAG, "Resumed"); }
}

void emulator_set_input(uint16_t button_mask)
{
    atomic_store(&input_state, button_mask);
}

const char *emulator_get_rom_path(void) { return rom_path_buf; }

emu_type_t emulator_get_type(void)
{
    return driver_to_type(current_driver);
}

/* ---- Save / Load state ---- */

static bool build_save_path(char *out, size_t out_size)
{
    const char *slash = strrchr(rom_path_buf, '/');
    const char *name  = slash ? slash + 1 : rom_path_buf;
    if (!name[0]) return false;

    mkdir(SAVE_STATE_PATH, 0775);

    char base[128];
    strncpy(base, name, sizeof(base) - 1);
    base[sizeof(base) - 1] = '\0';
    char *dot = strrchr(base, '.');
    if (dot) *dot = '\0';

    int n = snprintf(out, out_size, "%s/%s.sav", SAVE_STATE_PATH, base);
    return n > 0 && (size_t)n < out_size;
}

#define SAVE_MAGIC 0x45535653u  /* "ESAV" */

typedef struct {
    uint32_t magic;
    uint32_t emu_type;
    uint32_t data_size;
    uint32_t checksum;
} save_header_t;

static uint32_t calc_checksum(const uint8_t *data, size_t len)
{
    uint32_t crc = 0;
    for (size_t i = 0; i < len; i++) crc = (crc << 1) ^ data[i];
    return crc;
}

bool emulator_save_state(void)
{
    if (!running || !paused || !current_driver) {
        ESP_LOGE(TAG, "Cannot save: emulator not paused");
        return false;
    }

    char path[256];
    if (!build_save_path(path, sizeof(path))) {
        ESP_LOGE(TAG, "Failed to build save path");
        return false;
    }

    size_t state_size = current_driver->get_state_size();
    uint8_t *state_buf = heap_caps_malloc(state_size, MALLOC_CAP_SPIRAM);
    if (!state_buf) {
        ESP_LOGE(TAG, "Cannot allocate %u bytes for save state", (unsigned)state_size);
        return false;
    }

    current_driver->save_state(state_buf);

    save_header_t hdr = {
        .magic     = SAVE_MAGIC,
        .emu_type  = (uint32_t)driver_to_type(current_driver),
        .data_size = (uint32_t)state_size,
        .checksum  = calc_checksum(state_buf, state_size),
    };

    /* Atomic write: .tmp → rename */
    char tmp_path[260];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot create save file: %s", tmp_path);
        free(state_buf);
        return false;
    }

    bool ok = (fwrite(&hdr, sizeof(hdr), 1, f) == 1) &&
              (fwrite(state_buf, 1, state_size, f) == state_size);
    ok = (fclose(f) == 0) && ok;
    free(state_buf);

    if (!ok) {
        ESP_LOGE(TAG, "Save write failed");
        remove(tmp_path);
        return false;
    }

    remove(path);
    if (rename(tmp_path, path) != 0) {
        ESP_LOGE(TAG, "Save rename failed");
        return false;
    }

    ESP_LOGI(TAG, "State saved: %s (%u bytes)", path, (unsigned)state_size);
    return true;
}

bool emulator_load_state(void)
{
    if (!running || !paused || !current_driver) {
        ESP_LOGE(TAG, "Cannot load: emulator not paused");
        return false;
    }

    char path[256];
    if (!build_save_path(path, sizeof(path))) return false;

    FILE *f = fopen(path, "rb");
    if (!f) { ESP_LOGE(TAG, "No save file: %s", path); return false; }

    save_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) {
        ESP_LOGE(TAG, "Failed to read save header");
        fclose(f); return false;
    }

    if (hdr.magic != SAVE_MAGIC) {
        ESP_LOGE(TAG, "Invalid save magic: 0x%08lX", (unsigned long)hdr.magic);
        fclose(f); return false;
    }

    if ((emu_type_t)hdr.emu_type != driver_to_type(current_driver)) {
        ESP_LOGE(TAG, "Save type mismatch (save=%lu, current=%d)",
                 (unsigned long)hdr.emu_type, driver_to_type(current_driver));
        fclose(f); return false;
    }

    size_t expected = current_driver->get_state_size();
    if (hdr.data_size != (uint32_t)expected) {
        ESP_LOGE(TAG, "Save data size mismatch (%lu vs %u)",
                 (unsigned long)hdr.data_size, (unsigned)expected);
        fclose(f); return false;
    }

    uint8_t *state_buf = heap_caps_malloc(expected, MALLOC_CAP_SPIRAM);
    if (!state_buf) {
        ESP_LOGE(TAG, "Cannot allocate %u bytes for load", (unsigned)expected);
        fclose(f); return false;
    }

    bool ok = (fread(state_buf, 1, expected, f) == expected);
    fclose(f);

    if (!ok) {
        ESP_LOGE(TAG, "Failed to read save data");
        free(state_buf); return false;
    }

    uint32_t cksum = calc_checksum(state_buf, expected);
    if (cksum != hdr.checksum) {
        ESP_LOGE(TAG, "Save checksum mismatch");
        free(state_buf); return false;
    }

    bool result = current_driver->load_state(state_buf);
    free(state_buf);

    if (result) {
        ESP_LOGI(TAG, "State loaded: %s", path);
    } else {
        ESP_LOGE(TAG, "Emulator rejected save state");
    }
    return result;
}

bool emulator_has_save_state(void)
{
    char path[256];
    if (!build_save_path(path, sizeof(path))) return false;
    struct stat st;
    return stat(path, &st) == 0 && st.st_size > (long)sizeof(save_header_t);
}
