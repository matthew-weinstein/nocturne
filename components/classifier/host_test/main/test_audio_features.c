#include <math.h>
#include <stdio.h>
#include <string.h>
#include "audio_features.h"
#include "audio_features_vectors.h"
#include "tests.h"
#include "unity.h"

static audio_features_t features;
static int16_t samples[AUDIO_FEATURES_PATCH_SAMPLES];
static float patch[AUDIO_FEATURES_PATCH_FRAMES * AUDIO_FEATURES_MAX_BANDS];

static float max_difference(const float *expected, size_t num_values) {
    float worst = 0.0f;

    for (size_t i = 0; i < num_values; i++) {
        float difference = fabsf(patch[i] - expected[i]);
        if (difference > worst) {
            worst = difference;
        }
    }
    return worst;
}

static void check_vector(const audio_features_config_t *config, const float *expected, size_t num_values) {
    TEST_ASSERT_EQUAL(ESP_OK, audio_features_init(&features, config));
    audio_features_vector_samples(samples);
    audio_features_compute(&features, samples, patch);

    float worst = max_difference(expected, num_values);
    printf("%u bands, %.0f-%.0f Hz: largest difference from the Python reference %.2e\n",
           config->num_bands, config->min_hz, config->max_hz, worst);
    TEST_ASSERT_TRUE(worst < AUDIO_FEATURES_VECTOR_TOLERANCE);
}

static void test_yamnet_bands_match_the_python_reference(void) {
    check_vector(&audio_features_vector_yamnet_config, audio_features_vector_yamnet_patch,
                 AUDIO_FEATURES_VECTOR_YAMNET_VALUES);
}

static void test_another_band_config_matches_the_python_reference(void) {
    check_vector(&audio_features_vector_wide40_config, audio_features_vector_wide40_patch,
                 AUDIO_FEATURES_VECTOR_WIDE40_VALUES);
}

static void test_silence_is_the_log_offset_everywhere(void) {
    TEST_ASSERT_EQUAL(ESP_OK, audio_features_init(&features, &audio_features_vector_yamnet_config));
    memset(samples, 0, sizeof(samples));

    audio_features_compute(&features, samples, patch);

    float floor = logf(AUDIO_FEATURES_LOG_OFFSET);
    for (size_t i = 0; i < AUDIO_FEATURES_VECTOR_YAMNET_VALUES; i++) {
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, floor, patch[i]);
    }
}

static void test_full_scale_stays_finite(void) {
    TEST_ASSERT_EQUAL(ESP_OK, audio_features_init(&features, &audio_features_vector_yamnet_config));
    for (size_t n = 0; n < AUDIO_FEATURES_PATCH_SAMPLES; n++) {
        samples[n] = (n / 4) % 2 == 0 ? INT16_MAX : INT16_MIN;
    }

    audio_features_compute(&features, samples, patch);

    for (size_t i = 0; i < AUDIO_FEATURES_VECTOR_YAMNET_VALUES; i++) {
        TEST_ASSERT_TRUE(isfinite(patch[i]));
    }
}

static void test_every_yamnet_band_covers_a_bin(void) {
    TEST_ASSERT_EQUAL(ESP_OK, audio_features_init(&features, &audio_features_vector_yamnet_config));

    for (int band = 0; band < audio_features_vector_yamnet_config.num_bands; band++) {
        TEST_ASSERT_TRUE(features.band_num_bins[band] > 0);
    }
}

static void test_bad_configs_are_refused(void) {
    const audio_features_config_t bad[] = {
        {0, 125.0f, 7500.0f},
        {AUDIO_FEATURES_MAX_BANDS + 1, 125.0f, 7500.0f},
        {64, -1.0f, 7500.0f},
        {64, 125.0f, 8001.0f},
        {64, 7500.0f, 7500.0f},
        {64, 7500.0f, 125.0f},
    };

    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, audio_features_init(&features, &bad[i]));
    }
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, audio_features_init(&features, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, audio_features_init(NULL, &audio_features_vector_yamnet_config));
}

static void test_a_patch_spans_0_975_s_and_strides_0_96_s(void) {
    TEST_ASSERT_EQUAL_INT(15600, AUDIO_FEATURES_PATCH_SAMPLES);
    TEST_ASSERT_EQUAL_INT(15360, AUDIO_FEATURES_STRIDE_SAMPLES);
}

void run_audio_features_tests(void) {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_yamnet_bands_match_the_python_reference);
    RUN_TEST(test_another_band_config_matches_the_python_reference);
    RUN_TEST(test_silence_is_the_log_offset_everywhere);
    RUN_TEST(test_full_scale_stays_finite);
    RUN_TEST(test_every_yamnet_band_covers_a_bin);
    RUN_TEST(test_bad_configs_are_refused);
    RUN_TEST(test_a_patch_spans_0_975_s_and_strides_0_96_s);
}
