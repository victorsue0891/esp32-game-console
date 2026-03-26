#include "lcd_driver.h"
#include "config.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "lcd";

#define LCD_BUF_LINES  40  /* Number of lines per DMA transfer */

static spi_device_handle_t spi_handle;
static uint16_t *draw_buf = NULL;

/* ------- Low-level SPI helpers ------- */

static void lcd_cmd(uint8_t cmd)
{
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    gpio_set_level(LCD_PIN_DC, 0);
    spi_device_polling_transmit(spi_handle, &t);
}

static void lcd_data(const uint8_t *data, int len)
{
    if (len == 0) return;
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    gpio_set_level(LCD_PIN_DC, 1);
    spi_device_polling_transmit(spi_handle, &t);
}

/* Large pixel-data transfers: DMA-driven, yields the calling task while SPI runs */
static void lcd_data_large(const uint8_t *data, int len)
{
    if (len == 0) return;
    spi_transaction_t t = {
        .length = (size_t)len * 8,
        .tx_buffer = data,
    };
    gpio_set_level(LCD_PIN_DC, 1);
    spi_device_transmit(spi_handle, &t);  /* yields CPU to FreeRTOS scheduler */
}

static void lcd_data_byte(uint8_t val)
{
    lcd_data(&val, 1);
}

/* ------- Initialization Sequence ------- */

void lcd_init(void)
{
    ESP_LOGI(TAG, "Initializing ILI9341...");

    /* Configure DC and RST pins */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LCD_PIN_DC) | (1ULL << LCD_PIN_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    /* Hardware reset – ILI9341 only needs 10 µs low + 5 ms recovery */
    gpio_set_level(LCD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(20));

    /* SPI bus config */
    spi_bus_config_t buscfg = {
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = LCD_PIN_MISO,
        .sclk_io_num = LCD_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_BUF_LINES * 2 + 8,
    };
    spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);

    /* SPI device config */
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = LCD_SPI_FREQ_HZ,
        .mode = 0,
        .spics_io_num = LCD_PIN_CS,
        .queue_size = 7,
    };
    spi_bus_add_device(LCD_SPI_HOST, &devcfg, &spi_handle);

    /* ILI9341 init commands */
    lcd_cmd(0x01);  /* Software Reset */
    vTaskDelay(pdMS_TO_TICKS(50));

    lcd_cmd(0xCF);
    { uint8_t d[] = {0x00, 0xC1, 0x30}; lcd_data(d, sizeof(d)); }

    lcd_cmd(0xED);
    { uint8_t d[] = {0x64, 0x03, 0x12, 0x81}; lcd_data(d, sizeof(d)); }

    lcd_cmd(0xE8);
    { uint8_t d[] = {0x85, 0x00, 0x78}; lcd_data(d, sizeof(d)); }

    lcd_cmd(0xCB);
    { uint8_t d[] = {0x39, 0x2C, 0x00, 0x34, 0x02}; lcd_data(d, sizeof(d)); }

    lcd_cmd(0xF7);
    lcd_data_byte(0x20);

    lcd_cmd(0xEA);
    { uint8_t d[] = {0x00, 0x00}; lcd_data(d, sizeof(d)); }

    lcd_cmd(0xC0);  /* Power Control 1 */
    lcd_data_byte(0x23);

    lcd_cmd(0xC1);  /* Power Control 2 */
    lcd_data_byte(0x10);

    lcd_cmd(0xC5);  /* VCOM Control 1 */
    { uint8_t d[] = {0x3E, 0x28}; lcd_data(d, sizeof(d)); }

    lcd_cmd(0xC7);  /* VCOM Control 2 */
    lcd_data_byte(0x86);

    lcd_cmd(0x36);  /* Memory Access Control */
    lcd_data_byte(0x48);  /* Portrait mode */

    lcd_cmd(0x3A);  /* Pixel Format */
    lcd_data_byte(0x55);  /* 16-bit RGB565 */

    lcd_cmd(0xB1);  /* Frame Rate Control */
    { uint8_t d[] = {0x00, 0x18}; lcd_data(d, sizeof(d)); }

    lcd_cmd(0xB6);  /* Display Function Control */
    { uint8_t d[] = {0x08, 0x82, 0x27}; lcd_data(d, sizeof(d)); }

    lcd_cmd(0xF2);  /* 3Gamma Function Disable */
    lcd_data_byte(0x00);

    lcd_cmd(0x26);  /* Gamma Set */
    lcd_data_byte(0x01);

    lcd_cmd(0xE0);  /* Positive Gamma Correction */
    { uint8_t d[] = {0x0F,0x31,0x2B,0x0C,0x0E,0x08,0x4E,0xF1,
                     0x37,0x07,0x10,0x03,0x0E,0x09,0x00};
      lcd_data(d, sizeof(d)); }

    lcd_cmd(0xE1);  /* Negative Gamma Correction */
    { uint8_t d[] = {0x00,0x0E,0x14,0x03,0x11,0x07,0x31,0xC1,
                     0x48,0x08,0x0F,0x0C,0x31,0x36,0x0F};
      lcd_data(d, sizeof(d)); }

    lcd_cmd(0x11);  /* Sleep Out */
    vTaskDelay(pdMS_TO_TICKS(60));

    lcd_cmd(0x29);  /* Display On */
    vTaskDelay(pdMS_TO_TICKS(5));

    /* Allocate DMA-capable draw buffer in PSRAM */
    draw_buf = heap_caps_malloc(LCD_WIDTH * LCD_BUF_LINES * sizeof(uint16_t),
                                MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!draw_buf) {
        /* Fallback to PSRAM if internal DMA not available */
        draw_buf = heap_caps_malloc(LCD_WIDTH * LCD_BUF_LINES * sizeof(uint16_t),
                                    MALLOC_CAP_SPIRAM);
    }

    ESP_LOGI(TAG, "ILI9341 initialized (%dx%d)", LCD_WIDTH, LCD_HEIGHT);
}

static void lcd_set_window(int x1, int y1, int x2, int y2)
{
    lcd_cmd(0x2A);  /* Column Address Set */
    {
        uint8_t d[4] = {
            (x1 >> 8) & 0xFF, x1 & 0xFF,
            (x2 >> 8) & 0xFF, x2 & 0xFF
        };
        lcd_data(d, 4);
    }

    lcd_cmd(0x2B);  /* Page Address Set */
    {
        uint8_t d[4] = {
            (y1 >> 8) & 0xFF, y1 & 0xFF,
            (y2 >> 8) & 0xFF, y2 & 0xFF
        };
        lcd_data(d, 4);
    }

    lcd_cmd(0x2C);  /* Memory Write */
}

void lcd_flush_area(int x1, int y1, int x2, int y2,
                    const uint16_t *color_data)
{
    lcd_set_window(x1, y1, x2, y2);
    int len = (x2 - x1 + 1) * (y2 - y1 + 1);
    lcd_data_large((const uint8_t *)color_data, len * 2);
}

void lcd_flush(const uint16_t *buf, size_t len)
{
    lcd_set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
    lcd_data_large((const uint8_t *)buf, (int)len);
}

void lcd_set_backlight(int brightness)
{
    /* Simple on/off for now; PWM can be added later */
    (void)brightness;
}

uint16_t *lcd_get_draw_buffer(void)
{
    return draw_buf;
}
