#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * Initialize performance monitor (call after ui_init).
 */
void perf_monitor_init(void);

/**
 * Record one emulator frame (call from emulator loop for FPS tracking).
 */
void perf_monitor_record_frame(void);

/**
 * Get the measured FPS value.
 */
int perf_monitor_get_fps(void);

/**
 * Get free heap in bytes.
 */
int perf_monitor_get_free_heap(void);

/**
 * Toggle the FPS overlay on the emulator framebuffer.
 */
void perf_monitor_set_overlay(bool on);

/**
 * Check if overlay is enabled.
 */
bool perf_monitor_overlay_enabled(void);

/**
 * Draw FPS overlay onto an RGB565 framebuffer (top-right corner).
 * Call AFTER rendering each frame but BEFORE flushing to LCD.
 * @param fb    RGB565 framebuffer pointer.
 * @param w     Framebuffer width.
 * @param h     Framebuffer height.
 */
void perf_monitor_draw_overlay(uint16_t *fb, int w, int h);
