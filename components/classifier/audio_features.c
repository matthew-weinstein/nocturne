#include <math.h>
#include <stdbool.h>
#include <string.h>
#include "audio_features.h"

#define PCM_FULL_SCALE 32768.0
#define NYQUIST_HZ     (AUDIO_FEATURES_SAMPLE_RATE_HZ / 2.0)
#define MEL_BREAK_HZ   700.0
#define MEL_HIGH_Q     1127.0
#define TWO_PI         6.283185307179586

static double hertz_to_mel(double hertz) {
    return MEL_HIGH_Q * log(1.0 + hertz / MEL_BREAK_HZ);
}

static bool config_valid(const audio_features_config_t *config) {
    if (config->num_bands == 0 || config->num_bands > AUDIO_FEATURES_MAX_BANDS) {
        return false;
    }
    if (config->min_hz < 0.0f || config->max_hz > NYQUIST_HZ) {
        return false;
    }
    return config->min_hz < config->max_hz;
}

static void build_window(audio_features_t *features) {
    for (int n = 0; n < AUDIO_FEATURES_WINDOW_SAMPLES; n++) {
        double phase = TWO_PI * n / AUDIO_FEATURES_WINDOW_SAMPLES;
        double hann = 0.5 - 0.5 * cos(phase);
        features->scaled_window[n] = (float)(hann / PCM_FULL_SCALE);
    }
}

static void build_fft_tables(audio_features_t *features) {
    for (int k = 0; k < AUDIO_FEATURES_FFT_SIZE / 2; k++) {
        double phase = TWO_PI * k / AUDIO_FEATURES_FFT_SIZE;
        features->cos_table[k] = (float)cos(phase);
        features->sin_table[k] = (float)sin(phase);
    }

    for (int n = 0; n < AUDIO_FEATURES_FFT_SIZE; n++) {
        int reversed = 0;
        for (int bit = 0; bit < AUDIO_FEATURES_FFT_LOG2; bit++) {
            if (n & (1 << bit)) {
                reversed |= 1 << (AUDIO_FEATURES_FFT_LOG2 - 1 - bit);
            }
        }
        features->bit_reverse[n] = (uint16_t)reversed;
    }
}

static double band_weight(int bin, double lower_mel, double center_mel, double upper_mel) {
    double bin_hz = bin * NYQUIST_HZ / (AUDIO_FEATURES_NUM_BINS - 1);
    double bin_mel = hertz_to_mel(bin_hz);
    double rising = (bin_mel - lower_mel) / (center_mel - lower_mel);
    double falling = (upper_mel - bin_mel) / (upper_mel - center_mel);

    double weight = rising < falling ? rising : falling;
    return weight > 0.0 ? weight : 0.0;
}

static esp_err_t build_mel_bands(audio_features_t *features) {
    const audio_features_config_t *config = &features->config;
    double min_mel = hertz_to_mel(config->min_hz);
    double mel_step = (hertz_to_mel(config->max_hz) - min_mel) / (config->num_bands + 1);
    int num_weights = 0;

    for (int band = 0; band < config->num_bands; band++) {
        double lower_mel = min_mel + band * mel_step;
        double center_mel = lower_mel + mel_step;
        double upper_mel = center_mel + mel_step;

        features->band_first_weight[band] = (uint16_t)num_weights;
        features->band_first_bin[band] = 0;
        features->band_num_bins[band] = 0;

        for (int bin = 1; bin < AUDIO_FEATURES_NUM_BINS; bin++) {
            double weight = band_weight(bin, lower_mel, center_mel, upper_mel);
            if (weight <= 0.0) {
                continue;
            }
            if (num_weights == AUDIO_FEATURES_MAX_WEIGHTS) {
                return ESP_ERR_INVALID_SIZE;
            }
            if (features->band_num_bins[band] == 0) {
                features->band_first_bin[band] = (uint16_t)bin;
            }
            features->weights[num_weights] = (float)weight;
            features->band_num_bins[band]++;
            num_weights++;
        }
    }

    return ESP_OK;
}

esp_err_t audio_features_init(audio_features_t *features, const audio_features_config_t *config) {
    if (features == NULL || config == NULL || !config_valid(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(features, 0, sizeof(*features));
    features->config = *config;
    build_window(features);
    build_fft_tables(features);
    return build_mel_bands(features);
}

static void load_frame(audio_features_t *features, const int16_t *frame_samples) {
    memset(features->real, 0, sizeof(features->real));
    memset(features->imag, 0, sizeof(features->imag));

    for (int n = 0; n < AUDIO_FEATURES_WINDOW_SAMPLES; n++) {
        features->real[features->bit_reverse[n]] = frame_samples[n] * features->scaled_window[n];
    }
}

static void transform(audio_features_t *features) {
    float *real = features->real;
    float *imag = features->imag;

    for (int size = 2; size <= AUDIO_FEATURES_FFT_SIZE; size *= 2) {
        int half = size / 2;
        int table_step = AUDIO_FEATURES_FFT_SIZE / size;

        for (int start = 0; start < AUDIO_FEATURES_FFT_SIZE; start += size) {
            for (int k = 0; k < half; k++) {
                float twiddle_real = features->cos_table[k * table_step];
                float twiddle_imag = -features->sin_table[k * table_step];
                int top = start + k;
                int bottom = top + half;

                float product_real = twiddle_real * real[bottom] - twiddle_imag * imag[bottom];
                float product_imag = twiddle_real * imag[bottom] + twiddle_imag * real[bottom];

                real[bottom] = real[top] - product_real;
                imag[bottom] = imag[top] - product_imag;
                real[top] += product_real;
                imag[top] += product_imag;
            }
        }
    }

    for (int bin = 0; bin < AUDIO_FEATURES_NUM_BINS; bin++) {
        features->magnitude[bin] = sqrtf(real[bin] * real[bin] + imag[bin] * imag[bin]);
    }
}

static void log_mel_row(const audio_features_t *features, float *row) {
    for (int band = 0; band < features->config.num_bands; band++) {
        const float *weights = &features->weights[features->band_first_weight[band]];
        const float *magnitude = &features->magnitude[features->band_first_bin[band]];
        float mel = 0.0f;

        for (int i = 0; i < features->band_num_bins[band]; i++) {
            mel += weights[i] * magnitude[i];
        }
        row[band] = logf(mel + AUDIO_FEATURES_LOG_OFFSET);
    }
}

void audio_features_compute(audio_features_t *features, const int16_t *samples, float *patch) {
    for (int frame = 0; frame < AUDIO_FEATURES_PATCH_FRAMES; frame++) {
        load_frame(features, &samples[frame * AUDIO_FEATURES_HOP_SAMPLES]);
        transform(features);
        log_mel_row(features, &patch[frame * features->config.num_bands]);
    }
}
