#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t ring_buffer_init(size_t capacity_samples);
esp_err_t ring_buffer_write(const int16_t *samples, size_t num_samples);
esp_err_t ring_buffer_read(int16_t *out_samples, size_t num_samples);
size_t ring_buffer_available(void);
void ring_buffer_deinit(void);