#pragma once

#include <stddef.h>
#include <stdint.h>

#define PCM_CONVERT_SHIFT 9

void pcm_convert_from_i2s(const int32_t *samples, int16_t *out_pcm, size_t num_samples);
