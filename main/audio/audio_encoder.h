#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define OPUS_FRAME_SIZE_SAMPLES 320  // 20 ms at 16 kHz
#define OPUS_MAX_PACKET_BYTES   256

esp_err_t audio_encoder_init(void);
esp_err_t audio_encoder_encode_frame(const int16_t *pcm, uint8_t *out_bytes, size_t max_bytes, size_t *num_bytes);
esp_err_t audio_encoder_deinit(void); 