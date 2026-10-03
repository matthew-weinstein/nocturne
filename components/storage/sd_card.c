#include "sd_card.h"

#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_private/esp_gpio_reserve.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#define SD_CS_PIN   GPIO_NUM_10
#define SD_MOSI_PIN GPIO_NUM_11
#define SD_SCK_PIN  GPIO_NUM_12
#define SD_MISO_PIN GPIO_NUM_13

#define SD_SPI_HOST SPI2_HOST

static sdmmc_card_t *card;

esp_err_t sd_card_mount(void) {
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_MOSI_PIN,
        .miso_io_num = SD_MISO_PIN,
        .sclk_io_num = SD_SCK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    esp_err_t status = spi_bus_initialize(SD_SPI_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (status != ESP_OK) {
        return status;
    }

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = SD_CS_PIN;
    slot_cfg.host_id = SD_SPI_HOST;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    status = esp_vfs_fat_sdspi_mount(SD_CARD_MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &card);
    if (status != ESP_OK) {
        spi_bus_free(SD_SPI_HOST);
        return status;
    }

    sdmmc_card_print_info(stdout, card);
    return ESP_OK;
}

esp_err_t sd_card_unmount(void) {
    esp_gpio_revoke(BIT64(SD_CS_PIN));

    esp_err_t status = esp_vfs_fat_sdcard_unmount(SD_CARD_MOUNT_POINT, card);
    card = NULL;

    spi_bus_free(SD_SPI_HOST);
    return status;
}