#include "perf_monitor.h"
#include "config.h"

#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_log.h"

#include <stdatomic.h>
#include <string.h>

static const char *TAG = "perf";

/* ---- FPS tracking ---- */
static atomic_int frame_count = 0;
static int current_fps = 0;
static int64_t last_fps_time = 0;

/* ---- Overlay toggle ---- */
static bool overlay_on = false;

/* ---- Mini 3x5 pixel font for digits 0–9 ---- */
static const uint8_t font_3x5[10][5] = {
    {0x7, 0x5, 0x5, 0x5, 0x7},  /* 0 */
    {0x2, 0x6, 0x2, 0x2, 0x7},  /* 1 */
    {0x7, 0x1, 0x7, 0x4, 0x7},  /* 2 */
    {0x7, 0x1, 0x7, 0x1, 0x7},  /* 3 */
    {0x5, 0x5, 0x7, 0x1, 0x1},  /* 4 */
    {0x7, 0x4, 0x7, 0x1, 0x7},  /* 5 */
    {0x7, 0x4, 0x7, 0x5, 0x7},  /* 6 */
    {0x7, 0x1, 0x2, 0x2, 0x2},  /* 7 */
    {0x7, 0x5, 0x7, 0x5, 0x7},  /* 8 */
    {0x7, 0x5, 0x7, 0x1, 0x7},  /* 9 */
};

void perf_monitor_init(void)
{
    frame_count = 0;
    current_fps = 0;
    last_fps_time = esp_timer_get_time();
    overlay_on = false;
    ESP_LOGI(TAG, "Performance monitor initialized");
}

void perf_monitor_record_frame(void)
{
    atomic_fetch_add(&frame_count, 1);

    int64_t now = esp_timer_get_time();
    int64_t elapsed = now - last_fps_time;
    if (elapsed >= 1000000) { /* 1 second */
        current_fps = atomic_exchange(&frame_count, 0);
        last_fps_time = now;
    }
}

int perf_monitor_get_fps(void)
{
    return current_fps;
}

int perf_monitor_get_free_heap(void)
{
    return (int)esp_get_free_heap_size();
}

void perf_monitor_set_overlay(bool on)
{
    overlay_on = on;
    ESP_LOGI(TAG, "FPS overlay %s", on ? "ON" : "OFF");
}

bool perf_monitor_overlay_enabled(void)
{
    return overlay_on;
}

/* ---- Draw a digit at (x, y) on an RGB565 framebuffer ---- */
static void draw_digit(uint16_t *fb, int fb_w, int x, int y,
                       int digit, uint16_t color)
{
    if (digit < 0 || digit > 9) return;
    const uint8_t *glyph = font_3x5[digit];
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 3; col++) {
            if (glyph[row] & (1 << (2 - col))) {
                int px = x + col;
                int py = y + row;
                fb[py * fb_w + px] = color;
            }
        }
    }
}

void perf_monitor_draw_overlay(uint16_t *fb, int w, int h)
{
    if (!overlay_on || !fb) return;

    int fps = current_fps;

    /* Draw at top-right corner with 2px margin */
    /* Format: up to 3 digits (e.g. "60"), 4px per digit, right-aligned */
    int digits[3];
    int nd = 0;
    if (fps == 0) {
        digits[0] = 0;
        nd = 1;
    } else {
        int tmp = fps;
        while (tmp > 0 && nd < 3) {
            digits[nd++] = tmp % 10;
            tmp /= 10;
        }
    }

    /* Background rectangle */
    int box_w = nd * 4 + 2;
    int box_h = 7;
    int bx = w - box_w - 2;
    int by = 2;
    for (int yy = by; yy < by + box_h && yy < h; yy++) {
        for (int xx = bx; xx < bx + box_w && xx < w; xx++) {
            /* Semi-transparent dark background: blend with 50% black */
            uint16_t orig = fb[yy * w + xx];
            uint16_t r = ((orig >> 11) & 0x1F) >> 1;
            uint16_t g = ((orig >> 5) & 0x3F) >> 1;
            uint16_t b = (orig & 0x1F) >> 1;
            fb[yy * w + xx] = (r << 11) | (g << 5) | b;
        }
    }

    /* Draw digits right-to-left */
    uint16_t green = 0x07E0; /* bright green */
    int dx = bx + box_w - 4;
    for (int i = 0; i < nd; i++) {
        draw_digit(fb, w, dx, by + 1, digits[i], green);
        dx -= 4;
    }
}
