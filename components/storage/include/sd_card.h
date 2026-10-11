#pragma once

#include <stdint.h>
#include "esp_err.h"

#define SD_CARD_MOUNT_POINT "/sdcard"

esp_err_t sd_card_mount(void);
esp_err_t sd_card_unmount(void);
esp_err_t sd_card_free_pct(uint32_t *out_pct);
