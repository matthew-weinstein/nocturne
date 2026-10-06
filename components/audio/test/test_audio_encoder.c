#include <stdint.h>
#include <stdio.h>
#include "audio_encoder.h"
#include "audio_format.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "unity.h"

#define NUM_FRAMES        (1000 / AUDIO_FORMAT_FRAME_MS)
#define NOISE_SHIFT       18
#define BITRATE_TOLERANCE 0.3f
#define MAX_ENCODE_US     (AUDIO_FORMAT_FRAME_MS * 1000 / 2)

static int16_t pcm[AUDIO_FORMAT_FRAME_SAMPLES];
static uint8_t packet[AUDIO_FORMAT_MAX_PACKET_BYTES];
static uint32_t noise_state = 1;

static void fill_noise(void) {
    for (int i = 0; i < AUDIO_FORMAT_FRAME_SAMPLES; i++) {
        noise_state = noise_state * 1103515245u + 12345u;
        pcm[i] = (int16_t)((int32_t)noise_state >> NOISE_SHIFT);
    }
}

TEST_CASE("the encoder keeps up with capture at the configured bitrate", "[audio]") {
    TEST_ASSERT_EQUAL(ESP_OK, audio_encoder_init());

    size_t total_bytes = 0;
    int64_t total_us = 0;

    for (int frame = 0; frame < NUM_FRAMES; frame++) {
        fill_noise();

        size_t num_bytes = 0;
        int64_t started_us = esp_timer_get_time();
        TEST_ASSERT_EQUAL(ESP_OK, audio_encoder_encode_frame(pcm, packet, sizeof(packet), &num_bytes));
        total_us += esp_timer_get_time() - started_us;

        TEST_ASSERT_GREATER_THAN(0, num_bytes);
        total_bytes += num_bytes;
    }

    TEST_ASSERT_EQUAL(ESP_OK, audio_encoder_deinit());

    float bitrate = total_bytes * 8.0f;
    int average_us = (int)(total_us / NUM_FRAMES);
    printf("encoded 1 s of noise at %.0f bps, %d us per frame\n", bitrate, average_us);

    TEST_ASSERT_FLOAT_WITHIN(CONFIG_NOCTURNE_OPUS_BITRATE * BITRATE_TOLERANCE, CONFIG_NOCTURNE_OPUS_BITRATE, bitrate);
    TEST_ASSERT_LESS_THAN(MAX_ENCODE_US, average_us);
}
