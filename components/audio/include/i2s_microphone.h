#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t i2s_microphone_init(void);
esp_err_t i2s_microphone_read(int32_t *out_samples, size_t max_samples, size_t *num_samples);