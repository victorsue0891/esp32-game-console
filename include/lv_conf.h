/**
 * LVGL Configuration for ESP32-S3 Game Console
 * Based on lv_conf_template.h from LVGL v8.3
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#if 1 /* Set to 1 to enable content */

/* Color depth: 16-bit RGB565 for ILI9341 */
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 1  /* Byte-swap for SPI displays */

/* Memory */
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (64U * 1024U)  /* 64KB for LVGL internal heap */

/* Display */
#define LV_DPI_DEF 130

/* Tick */
#define LV_TICK_CUSTOM 0

/* Logging */
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

/* GPU - none */
#define LV_USE_GPU_STM32_DMA2D 0
#define LV_USE_GPU_NXP_PXP 0
#define LV_USE_GPU_NXP_VG_LITE 0

/* File system (for loading images from SD card) */
#define LV_USE_FS_STDIO 1
#define LV_FS_STDIO_LETTER 'S'
#define LV_FS_STDIO_PATH ""
#define LV_FS_STDIO_CACHE_SIZE 0

/* Image decoders */
#define LV_USE_SJPG 1   /* JPEG support */
#define LV_USE_PNG 0
#define LV_USE_BMP 0
#define LV_USE_GIF 0

/* Widgets */
#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_BTN        1
#define LV_USE_BTNMATRIX  1
#define LV_USE_CANVAS     0
#define LV_USE_CHECKBOX   0
#define LV_USE_DROPDOWN   0
#define LV_USE_IMG        1
#define LV_USE_LABEL      1
#define LV_USE_LINE       0
#define LV_USE_ROLLER     0
#define LV_USE_SLIDER     0
#define LV_USE_SWITCH     0
#define LV_USE_TEXTAREA   1
#define LV_USE_TABLE      0

/* Extra widgets */
#define LV_USE_LIST       1
#define LV_USE_MENU       0
#define LV_USE_METER      0
#define LV_USE_MSGBOX     0
#define LV_USE_SPAN       0
#define LV_USE_SPINBOX    0
#define LV_USE_SPINNER    0
#define LV_USE_TABVIEW    0
#define LV_USE_TILEVIEW   0
#define LV_USE_WIN        0
#define LV_USE_CALENDAR   0
#define LV_USE_CALENDAR_HEADER_ARROW  0
#define LV_USE_CALENDAR_HEADER_DROPDOWN 0
#define LV_USE_CHART      0
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN     0
#define LV_USE_KEYBOARD   0
#define LV_USE_LED        0
#define LV_USE_ANIMIMG    0

/* Layouts */
#define LV_USE_FLEX 1
#define LV_USE_GRID 0

/* Themes */
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

/* Animations */
#define LV_USE_ANIM 1

/* Font */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#endif /* LV_CONF_H */
#endif /* 1 */
