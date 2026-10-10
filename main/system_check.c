#include "system_check.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "opus.h"

#include "aes_gcm.h"
#include "audio_format.h"
#include "key_manager.h"
#include "manifest.h"
#include "segment_format.h"
#include "session.h"

#define TAG "system_check"

#define PATH_LEN             128
#define FILENAME_LEN         32
#define LENGTH_FIELD_BYTES   4
#define KEY_READY_TIMEOUT_MS 30000

static uint8_t record[SEGMENT_FORMAT_MAX_CHUNK_RECORD_BYTES];
static uint8_t plaintext[SEGMENT_FORMAT_MAX_CHUNK_PLAINTEXT_BYTES];
static int16_t pcm[AUDIO_FORMAT_FRAME_SAMPLES];
static manifest_t manifest;

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static bool has_audio(int num_samples) {
    for (int i = 0; i < num_samples; i++) {
        if (pcm[i] != 0) {
            return true;
        }
    }
    return false;
}

static esp_err_t decode_chunk(OpusDecoder *decoder, size_t num_bytes, int *frames, int *audible_frames) {
    size_t offset = 0;

    while (offset < num_bytes) {
        if (offset + SEGMENT_FORMAT_FRAME_LENGTH_BYTES > num_bytes) {
            ESP_LOGE(TAG, "frame %d: length prefix cut off", *frames);
            return ESP_FAIL;
        }

        uint16_t length = (uint16_t)(plaintext[offset] | (plaintext[offset + 1] << 8));
        offset += SEGMENT_FORMAT_FRAME_LENGTH_BYTES;

        if (length == 0 || length > AUDIO_FORMAT_MAX_PACKET_BYTES || offset + length > num_bytes) {
            ESP_LOGE(TAG, "frame %d: implausible length %u", *frames, (unsigned)length);
            return ESP_FAIL;
        }

        int num_samples = opus_decode(decoder, plaintext + offset, length, pcm, AUDIO_FORMAT_FRAME_SAMPLES, 0);
        if (num_samples != AUDIO_FORMAT_FRAME_SAMPLES) {
            ESP_LOGE(TAG, "frame %d: decoded %d samples", *frames, num_samples);
            return ESP_FAIL;
        }

        if (has_audio(num_samples)) {
            (*audible_frames)++;
        }

        offset += length;
        (*frames)++;
    }

    return ESP_OK;
}

static esp_err_t check_header(FILE *file) {
    uint8_t expected[SEGMENT_FORMAT_HEADER_BYTES];
    uint8_t header[SEGMENT_FORMAT_HEADER_BYTES];
    segment_format_header(expected);

    if (fread(header, 1, sizeof(header), file) != sizeof(header) || memcmp(header, expected, sizeof(header)) != 0) {
        ESP_LOGE(TAG, "bad segment header");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t check_chunks(FILE *file, uint16_t segment_index, OpusDecoder *decoder, int *frames) {
    int audible_frames = 0;
    uint32_t chunk_index = 0;

    while (fread(record, 1, LENGTH_FIELD_BYTES, file) == LENGTH_FIELD_BYTES) {
        size_t num_record_bytes = read_le32(record) + AES_GCM_RECORD_OVERHEAD_BYTES;
        if (num_record_bytes > sizeof(record)) {
            ESP_LOGE(TAG, "chunk %u: implausible length", (unsigned)chunk_index);
            return ESP_FAIL;
        }

        size_t num_rest_bytes = num_record_bytes - LENGTH_FIELD_BYTES;
        if (fread(record + LENGTH_FIELD_BYTES, 1, num_rest_bytes, file) != num_rest_bytes) {
            ESP_LOGE(TAG, "chunk %u: torn", (unsigned)chunk_index);
            return ESP_FAIL;
        }

        size_t num_plaintext_bytes = 0;
        size_t consumed_bytes = 0;
        esp_err_t status = aes_gcm_open_chunk(key_manager_key(), segment_index, chunk_index, record,
                                              num_record_bytes, plaintext, sizeof(plaintext),
                                              &num_plaintext_bytes, &consumed_bytes);
        if (status != ESP_OK) {
            ESP_LOGE(TAG, "chunk %u: failed to open (%s)", (unsigned)chunk_index, esp_err_to_name(status));
            return ESP_FAIL;
        }

        if (decode_chunk(decoder, num_plaintext_bytes, frames, &audible_frames) != ESP_OK) {
            return ESP_FAIL;
        }

        chunk_index++;
    }

    if (audible_frames <= *frames / 2) {
        ESP_LOGE(TAG, "only %d of %d frames contain audio", audible_frames, *frames);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t check_segment(const char *session_dir, int index, OpusDecoder *decoder, int *frames) {
    char filename[FILENAME_LEN];
    manifest_segment_filename(index, filename, sizeof(filename));

    char path[PATH_LEN];
    snprintf(path, sizeof(path), "%s/%s", session_dir, filename);

    struct stat info;
    if (stat(path, &info) != 0) {
        ESP_LOGE(TAG, "%s is missing", path);
        return ESP_FAIL;
    }

    size_t expected_bytes = manifest.segments[index].num_bytes;
    if ((size_t)info.st_size != expected_bytes) {
        ESP_LOGE(TAG, "%s is %ld bytes, manifest says %u", path, (long)info.st_size, (unsigned)expected_bytes);
        return ESP_FAIL;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "could not open %s", path);
        return ESP_FAIL;
    }

    esp_err_t status = check_header(file);
    if (status == ESP_OK) {
        status = check_chunks(file, (uint16_t)index, decoder, frames);
    }

    fclose(file);
    return status;
}

static esp_err_t check_segments(const char *session_dir) {
    int opus_error = OPUS_OK;
    OpusDecoder *decoder = opus_decoder_create(AUDIO_FORMAT_SAMPLE_RATE_HZ, 1, &opus_error);
    if (decoder == NULL) {
        ESP_LOGE(TAG, "opus_decoder_create returned %d", opus_error);
        return ESP_FAIL;
    }

    esp_err_t result = ESP_OK;

    for (int i = 0; i < manifest.num_segments; i++) {
        int frames = 0;
        esp_err_t status = check_segment(session_dir, i, decoder, &frames);
        printf("NCT_CHECK segment=%d frames=%d status=%s\n", i, frames, status == ESP_OK ? "ok" : "fail");

        if (status != ESP_OK) {
            result = ESP_FAIL;
        }
    }

    opus_decoder_destroy(decoder);
    return result;
}

static esp_err_t check_session(void) {
    const char *session_dir = session_current_dir();
    if (session_dir[0] == '\0') {
        ESP_LOGE(TAG, "no session has been started");
        return ESP_FAIL;
    }

    if (key_manager_wait_ready(KEY_READY_TIMEOUT_MS) != ESP_OK) {
        ESP_LOGE(TAG, "key not ready");
        return ESP_FAIL;
    }

    char manifest_path[PATH_LEN];
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.bin", session_dir);

    esp_err_t status = manifest_open(&manifest, manifest_path);
    if (status != ESP_OK) {
        ESP_LOGE(TAG, "manifest_open returned %s", esp_err_to_name(status));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "%s: %d segments", session_dir, manifest.num_segments);
    return check_segments(session_dir);
}

esp_err_t system_check_session(void) {
    esp_err_t status = check_session();
    printf("NCT_CHECK result=%s\n", status == ESP_OK ? "PASS" : "FAIL");
    return status;
}
