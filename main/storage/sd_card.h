#pragma once

#include "esp_err.h"

#define SD_CARD_MOUNT_POINT "/sdcard"

esp_err_t sd_card_mount(void);
esp_err_t sd_card_unmount(void);