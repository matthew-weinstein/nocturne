#include "audio_encoder.h"

#include "opus.h"
#include "i2s_microphone.h"

#define OPUS_BITRATE_BPS 24000
#define OPUS_COMPLEXITY  1
#define OPUS_CHANNELS    1

static OpusEncoder *encoder;

esp_err_t audio_encoder_init(void) {
    int opus_error = OPUS_OK;

    encoder = opus_encoder_create(MICROPHONE_SAMPLE_RATE_HZ,
                                  OPUS_CHANNELS,
                                  OPUS_APPLICATION_VOIP,
                                  &opus_error);

    if (encoder == NULL || opus_error != OPUS_OK) {
        return ESP_FAIL;
    }

    if (opus_encoder_ctl(encoder, OPUS_SET_BITRATE(OPUS_BITRATE_BPS)) != OPUS_OK) {
        return ESP_FAIL;
    }

    if (opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(OPUS_COMPLEXITY)) != OPUS_OK) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t audio_encoder_encode_frame(const int16_t *pcm, uint8_t *out_bytes, size_t max_bytes, size_t *num_bytes) {
    opus_int32 encoded = opus_encode(encoder, pcm, OPUS_FRAME_SIZE_SAMPLES, out_bytes, (opus_int32)max_bytes);

    if (encoded < 0) {
        return ESP_FAIL;
    }

    *num_bytes = (size_t)encoded;
    return ESP_OK;
}

esp_err_t audio_encoder_deinit(void) {
    if (encoder != NULL) {
        opus_encoder_destroy(encoder);
        encoder = NULL;
    }
    return ESP_OK;
}