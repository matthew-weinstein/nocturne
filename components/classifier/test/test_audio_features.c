#include <math.h>
#include <stdio.h>
#include "audio_features.h"
#include "audio_features_vectors.h"
#include "esp_timer.h"
#include "unity.h"

#define PATCH_BUDGET_US 100000

static audio_features_t features;
static int16_t samples[AUDIO_FEATURES_PATCH_SAMPLES];
static float patch[AUDIO_FEATURES_VECTOR_YAMNET_VALUES];

TEST_CASE("log-mel patch on the FPU matches the Python reference within budget", "[classifier]") {
    TEST_ASSERT_EQUAL(ESP_OK, audio_features_init(&features, &audio_features_vector_yamnet_config));
    audio_features_vector_samples(samples);

    int64_t start_us = esp_timer_get_time();
    audio_features_compute(&features, samples, patch);
    int64_t elapsed_us = esp_timer_get_time() - start_us;

    float worst = 0.0f;
    for (size_t i = 0; i < AUDIO_FEATURES_VECTOR_YAMNET_VALUES; i++) {
        float difference = fabsf(patch[i] - audio_features_vector_yamnet_patch[i]);
        if (difference > worst) {
            worst = difference;
        }
    }

    printf("patch in %lld us, largest difference %.2e\n", (long long)elapsed_us, worst);
    TEST_ASSERT_TRUE(worst < AUDIO_FEATURES_VECTOR_TOLERANCE);
    TEST_ASSERT_TRUE(elapsed_us < PATCH_BUDGET_US);
}
