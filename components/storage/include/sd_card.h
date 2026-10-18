#pragma once

#include <stdint.h>
#include "esp_err.h"

#define SD_CARD_MOUNT_POINT "/sdcard"

#define SD_CARD_WRITE_RETRIES     3
#define SD_CARD_RETRY_DELAY_US    100000

esp_err_t sd_card_mount(void);
esp_err_t sd_card_unmount(void);
esp_err_t sd_card_free_pct(uint32_t *out_pct);
