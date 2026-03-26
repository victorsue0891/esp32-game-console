#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * Initialize the ILI9341 LCD via SPI.
 */
void lcd_init(void);

/**
 * Send a full-screen framebuffer (RGB565) to the display.
 * Buffer size must be LCD_WIDTH * LCD_HEIGHT * 2 bytes.
 */
void lcd_flush(const uint16_t *buf, size_t len);

/**
 * Send a partial region to the display.
 */
void lcd_flush_area(int x1, int y1, int x2, int y2,
                    const uint16_t *color_data);

/**
 * Set backlight brightness (0-100). Currently binary on/off.
 */
void lcd_set_backlight(int brightness);

/**
 * Get the DMA-capable draw buffer pointer. Size = LCD_WIDTH * BUFFER_LINES * 2.
 */
uint16_t *lcd_get_draw_buffer(void);
