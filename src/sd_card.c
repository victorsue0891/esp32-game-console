#include "sd_card.h"
#include "config.h"

#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_log.h"

#include <sys/stat.h>

static const char *TAG = "sd_card";
static bool mounted = false;
static sdmmc_card_t *card = NULL;

esp_err_t sd_card_init(void)
{
    ESP_LOGI(TAG, "Initializing SD card (SPI mode)...");

    /* SPI bus for SD card (SPI3) */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_PIN_MOSI,
        .miso_io_num = SD_PIN_MISO,
        .sclk_io_num = SD_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    esp_err_t ret = spi_bus_initialize(SD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Mount configuration */
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    /* SD SPI device configuration */
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_PIN_CS;
    slot_config.host_id = SD_SPI_HOST;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;

    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host,
                                   &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    mounted = true;
    sdmmc_card_print_info(stdout, card);
    ESP_LOGI(TAG, "SD card mounted at %s", SD_MOUNT_POINT);
    return ESP_OK;
}

bool sd_card_is_mounted(void)
{
    return mounted;
}

bool sd_card_check_health(void)
{
    if (!mounted) return false;

    /* Try to stat the mount point to verify the card is still accessible */
    struct stat st;
    if (stat(SD_MOUNT_POINT, &st) != 0) {
        ESP_LOGW(TAG, "SD card health check failed - card may be removed");
        mounted = false;
        return false;
    }
    return true;
}

void sd_card_deinit(void)
{
    if (mounted) {
        esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, card);
        spi_bus_free(SD_SPI_HOST);
        mounted = false;
        card = NULL;
        ESP_LOGI(TAG, "SD card unmounted");
    }
}
