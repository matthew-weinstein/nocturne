#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t sntp_time_init(void);
bool sntp_time_is_reliable(void);
esp_err_t sntp_time_wait_reliable(uint32_t timeout_ms);
bool sntp_time_survived_reset(void);
