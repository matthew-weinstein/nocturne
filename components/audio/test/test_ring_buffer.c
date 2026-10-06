#include <stdint.h>
#include "audio_format.h"
#include "esp_heap_caps.h"
#include "ring_buffer.h"
#include "unity.h"

#define CAPACITY_SAMPLES (AUDIO_FORMAT_SAMPLE_RATE_HZ * 10)
#define BLOCK_SAMPLES    500

static int16_t block[BLOCK_SAMPLES];
static int16_t samples[BLOCK_SAMPLES];

static void fill_block(size_t first_sample) {
    for (size_t i = 0; i < BLOCK_SAMPLES; i++) {
        block[i] = (int16_t)(first_sample + i);
    }
}

TEST_CASE("the ring buffer holds 10 s of audio in PSRAM", "[audio]") {
    size_t free_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    TEST_ASSERT_EQUAL(ESP_OK, ring_buffer_init(CAPACITY_SAMPLES));
    size_t free_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    TEST_ASSERT_GREATER_OR_EQUAL(CAPACITY_SAMPLES * sizeof(int16_t), free_before - free_after);

    for (size_t num_written = 0; num_written < CAPACITY_SAMPLES; num_written += BLOCK_SAMPLES) {
        fill_block(num_written);
        TEST_ASSERT_EQUAL(ESP_OK, ring_buffer_write(block, BLOCK_SAMPLES));
    }
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, ring_buffer_write(block, 1));

    for (size_t num_read = 0; num_read < CAPACITY_SAMPLES; num_read += BLOCK_SAMPLES) {
        fill_block(num_read);
        TEST_ASSERT_EQUAL(ESP_OK, ring_buffer_read(samples, BLOCK_SAMPLES));
        TEST_ASSERT_EQUAL_INT16_ARRAY(block, samples, BLOCK_SAMPLES);
    }

    ring_buffer_deinit();
}
