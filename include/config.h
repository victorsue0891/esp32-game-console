#pragma once

/* ============================================================
 * ESP32-S3 Retro Handheld Game Console - Hardware Configuration
 * ============================================================ */

#include "driver/gpio.h"

/* --- LCD (ILI9341) SPI Pins --- */
#define LCD_PIN_MOSI    GPIO_NUM_11
#define LCD_PIN_MISO    GPIO_NUM_13
#define LCD_PIN_SCK     GPIO_NUM_12
#define LCD_PIN_CS      GPIO_NUM_10
#define LCD_PIN_DC      GPIO_NUM_9
#define LCD_PIN_RST     GPIO_NUM_14

#define LCD_WIDTH       240
#define LCD_HEIGHT      320
#define LCD_SPI_HOST    SPI2_HOST
#define LCD_SPI_FREQ_HZ (40 * 1000 * 1000)   /* 40 MHz */

/* --- SD Card SPI Pins --- */
#define SD_PIN_MOSI     GPIO_NUM_35
#define SD_PIN_MISO     GPIO_NUM_37
#define SD_PIN_SCK      GPIO_NUM_36
#define SD_PIN_CS       GPIO_NUM_38
#define SD_SPI_HOST     SPI3_HOST
#define SD_MOUNT_POINT  "/sdcard"

/* --- I2S Audio (MAX98357) Pins --- */
#define I2S_PIN_BCK     GPIO_NUM_17
#define I2S_PIN_LRCK    GPIO_NUM_18
#define I2S_PIN_DOUT    GPIO_NUM_16
#define I2S_SAMPLE_RATE 22050
#define I2S_PORT_NUM    I2S_NUM_0

/* --- Button GPIO Pins --- */
#define BTN_UP          GPIO_NUM_1
#define BTN_DOWN        GPIO_NUM_2
#define BTN_LEFT        GPIO_NUM_3
#define BTN_RIGHT       GPIO_NUM_4
#define BTN_A           GPIO_NUM_5
#define BTN_B           GPIO_NUM_6
#define BTN_START       GPIO_NUM_7
#define BTN_SELECT      GPIO_NUM_8
#define BTN_VOL_UP      GPIO_NUM_21
#define BTN_VOL_DOWN    GPIO_NUM_47

/* --- Volume Defaults --- */
#define VOLUME_DEFAULT  30   /* 30% */
#define VOLUME_MAX      100
#define VOLUME_MIN      0
#define VOLUME_STEP     5
#define VOLUME_OSD_TIMEOUT_MS  5000

/* --- LVGL Tick --- */
#define LV_TICK_PERIOD_MS 2

/* --- Battery ADC (GPIO15 = ADC2_CH4 on ESP32-S3) --- */
#define BAT_ADC_GPIO        GPIO_NUM_15
#define BAT_ADC_CHANNEL     ADC_CHANNEL_4
#define BAT_ADC_UNIT        ADC_UNIT_2
#define BAT_VDIV_RATIO      2.0f            /* Voltage divider ratio */
#define BAT_FULL_MV         4200            /* Full charge mV */
#define BAT_EMPTY_MV        3300            /* Empty mV */
#define BAT_READ_INTERVAL_MS 10000          /* Read every 10s */

/* --- WiFi --- */
#define WIFI_CONFIG_PATH    SD_MOUNT_POINT "/system/wifi.cfg"
#define WIFI_MAX_RETRY      5
#define WIFI_CONNECT_TIMEOUT_MS 15000

/* --- Performance Monitor --- */
#define PERF_UPDATE_INTERVAL_MS 1000

/* --- ROM Paths --- */
#define ROM_NES_PATH    SD_MOUNT_POINT "/roms/nes"
#define ROM_GB_PATH     SD_MOUNT_POINT "/roms/gb"
#define BOOT_IMAGE_PATH SD_MOUNT_POINT "/system/boot.jpg"
#define SAVE_STATE_PATH SD_MOUNT_POINT "/saves"
