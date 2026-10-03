#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct wav_writer wav_writer_t;

esp_err_t wav_writer_open(wav_writer_t **writer, const char *path, uint32_t sample_rate);
esp_err_t wav_writer_write(wav_writer_t *writer, const int16_t *samples, size_t num_samples);
esp_err_t wav_writer_close(wav_writer_t *writer);