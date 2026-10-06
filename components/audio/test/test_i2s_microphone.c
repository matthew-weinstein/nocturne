#include <stdint.h>
#include "audio_format.h"
#include "i2s_microphone.h"
#include "unity.h"

#define SETTLE_SAMPLES  (AUDIO_FORMAT_SAMPLE_RATE_HZ / 2)
#define CAPTURE_SAMPLES AUDIO_FORMAT_SAMPLE_RATE_HZ
#define BLOCK_SAMPLES   512

static int32_t block[BLOCK_SAMPLES];

static size_t read_block(void) {
    size_t num_samples = 0;
    TEST_ASSERT_EQUAL(ESP_OK, i2s_microphone_read(block, BLOCK_SAMPLES, &num_samples));
    return num_samples;
}

TEST_CASE("the microphone delivers a second of live samples", "[audio]") {
    TEST_ASSERT_EQUAL(ESP_OK, i2s_microphone_init());

    size_t settled = 0;
    while (settled < SETTLE_SAMPLES) {
        settled += read_block();
    }

    size_t captured = 0;
    size_t num_nonzero = 0;
    int32_t min = INT32_MAX;
    int32_t max = INT32_MIN;

    while (captured < CAPTURE_SAMPLES) {
        size_t num_samples = read_block();

        for (size_t i = 0; i < num_samples; i++) {
            if (block[i] != 0) {
                num_nonzero++;
            }
            if (block[i] < min) {
                min = block[i];
            }
            if (block[i] > max) {
                max = block[i];
            }
        }

        captured += num_samples;
    }

    TEST_ASSERT_GREATER_THAN(captured / 2, num_nonzero);
    TEST_ASSERT_GREATER_THAN(min, max);
}
