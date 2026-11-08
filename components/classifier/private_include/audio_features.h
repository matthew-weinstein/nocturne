#pragma once

#include <stdint.h>
#include "esp_err.h"

#define AUDIO_FEATURES_SAMPLE_RATE_HZ 16000
#define AUDIO_FEATURES_WINDOW_SAMPLES 400
#define AUDIO_FEATURES_HOP_SAMPLES    160
#define AUDIO_FEATURES_FFT_SIZE       512
#define AUDIO_FEATURES_FFT_LOG2       9
#define AUDIO_FEATURES_NUM_BINS       (AUDIO_FEATURES_FFT_SIZE / 2 + 1)
#define AUDIO_FEATURES_PATCH_FRAMES   96
#define AUDIO_FEATURES_PATCH_SAMPLES  ((AUDIO_FEATURES_PATCH_FRAMES - 1) * AUDIO_FEATURES_HOP_SAMPLES + AUDIO_FEATURES_WINDOW_SAMPLES)
#define AUDIO_FEATURES_STRIDE_SAMPLES (AUDIO_FEATURES_PATCH_FRAMES * AUDIO_FEATURES_HOP_SAMPLES)
#define AUDIO_FEATURES_LOG_OFFSET     0.001f
#define AUDIO_FEATURES_MAX_BANDS      128
#define AUDIO_FEATURES_MAX_WEIGHTS    (2 * AUDIO_FEATURES_NUM_BINS)

typedef struct {
    uint16_t num_bands;
    float min_hz;
    float max_hz;
} audio_features_config_t;

typedef struct {
    audio_features_config_t config;
    float scaled_window[AUDIO_FEATURES_WINDOW_SAMPLES];
    float cos_table[AUDIO_FEATURES_FFT_SIZE / 2];
    float sin_table[AUDIO_FEATURES_FFT_SIZE / 2];
    uint16_t bit_reverse[AUDIO_FEATURES_FFT_SIZE];
    uint16_t band_first_bin[AUDIO_FEATURES_MAX_BANDS];
    uint16_t band_num_bins[AUDIO_FEATURES_MAX_BANDS];
    uint16_t band_first_weight[AUDIO_FEATURES_MAX_BANDS];
    float weights[AUDIO_FEATURES_MAX_WEIGHTS];
    float real[AUDIO_FEATURES_FFT_SIZE];
    float imag[AUDIO_FEATURES_FFT_SIZE];
    float magnitude[AUDIO_FEATURES_NUM_BINS];
} audio_features_t;

esp_err_t audio_features_init(audio_features_t *features, const audio_features_config_t *config);
void audio_features_compute(audio_features_t *features, const int16_t *samples, float *patch);
