#include "self_test.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "opus.h"

#include "audio_encoder.h"
#include "i2s_microphone.h"
#include "manifest.h"
#include "sd_card.h"

#define TAG "selftest"

#define SESSIONS_DIR       SD_CARD_MOUNT_POINT "/sessions"
#define TEST_MANIFEST_PATH SD_CARD_MOUNT_POINT "/test_manifest.bin"

#define PATH_LEN 128

#define CHECK(condition, ...)                       \
    do {                                            \
        if (!(condition)) {                         \
            ESP_LOGE(TAG, "FAIL: " __VA_ARGS__);    \
            return ESP_FAIL;                        \
        }                                           \
    } while (0)

/* ---------- manifest round-trip ---------- */

static esp_err_t manifest_write_known(void) {
    manifest_t manifest;

    esp_err_t status = manifest_create(&manifest, TEST_MANIFEST_PATH, "20260101_0000", 1767225600);
    CHECK(status == ESP_OK, "manifest_create returned %d", status);

    CHECK(manifest_record_segment(&manifest, TEST_MANIFEST_PATH, 900000) == ESP_OK, "record segment 0");
    CHECK(manifest_record_segment(&manifest, TEST_MANIFEST_PATH, 899000) == ESP_OK, "record segment 1");
    CHECK(manifest_record_segment(&manifest, TEST_MANIFEST_PATH, 901000) == ESP_OK, "record segment 2");
    CHECK(manifest_record_upload(&manifest, TEST_MANIFEST_PATH, 0) == ESP_OK, "record upload 0");
    CHECK(manifest_record_upload(&manifest, TEST_MANIFEST_PATH, 2) == ESP_OK, "record upload 2");

    return ESP_OK;
}

static esp_err_t manifest_verify_replay(bool expect_complete) {
    manifest_t manifest;

    esp_err_t status = manifest_open(&manifest, TEST_MANIFEST_PATH);
    CHECK(status == ESP_OK, "manifest_open returned %d", status);

    CHECK(strcmp(manifest.session_id, "20260101_0000") == 0, "session_id was '%s'", manifest.session_id);
    CHECK(manifest.started_unix == 1767225600, "started_unix was %lld", (long long)manifest.started_unix);
    CHECK(manifest.num_segments == 3, "num_segments was %d, expected 3", manifest.num_segments);
    CHECK(manifest.complete == expect_complete, "complete was %d, expected %d", manifest.complete, expect_complete);

    CHECK(manifest.segments[0].num_bytes == 900000, "segment 0 bytes");
    CHECK(manifest.segments[1].num_bytes == 899000, "segment 1 bytes");
    CHECK(manifest.segments[2].num_bytes == 901000, "segment 2 bytes");

    CHECK(manifest.segments[0].uploaded == true,  "segment 0 uploaded");
    CHECK(manifest.segments[1].uploaded == false, "segment 1 not uploaded");
    CHECK(manifest.segments[2].uploaded == true,  "segment 2 uploaded");

    return ESP_OK;
}

static esp_err_t manifest_append_torn_record(void) {
    FILE *file = fopen(TEST_MANIFEST_PATH, "ab");
    CHECK(file != NULL, "could not open manifest to append torn record");

    const uint8_t fragment[3] = { 0x01, 0x03, 0x00 };
    size_t written = fwrite(fragment, 1, sizeof(fragment), file);
    fflush(file);
    fclose(file);

    CHECK(written == sizeof(fragment), "torn append wrote %u bytes", (unsigned)written);
    return ESP_OK;
}

esp_err_t self_test_manifest(void) {
    ESP_LOGI(TAG, "manifest: writing known records");
    if (manifest_write_known() != ESP_OK) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "manifest: replaying");
    if (manifest_verify_replay(false) != ESP_OK) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "manifest: appending 3-byte torn record");
    if (manifest_append_torn_record() != ESP_OK) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "manifest: replaying again, torn record must be discarded");
    if (manifest_verify_replay(false) != ESP_OK) {
        return ESP_FAIL;
    }

    manifest_t manifest;
    if (manifest_open(&manifest, TEST_MANIFEST_PATH) != ESP_OK) {
        return ESP_FAIL;
    }
    CHECK(manifest_record_complete(&manifest, TEST_MANIFEST_PATH) == ESP_OK, "record complete after torn tail");

    ESP_LOGI(TAG, "manifest: replaying after completion");
    if (manifest_verify_replay(true) != ESP_OK) {
        return ESP_FAIL;
    }

    remove(TEST_MANIFEST_PATH);
    ESP_LOGI(TAG, "manifest: PASS");
    return ESP_OK;
}

/* ---------- session verification ---------- */

static esp_err_t find_latest_session(char *out, size_t out_len) {
    DIR *dir = opendir(SESSIONS_DIR);
    CHECK(dir != NULL, "could not open %s", SESSIONS_DIR);

    char newest[64] = { 0 };
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        if (strcmp(entry->d_name, newest) > 0) {
            strncpy(newest, entry->d_name, sizeof(newest) - 1);
        }
    }

    closedir(dir);
    CHECK(newest[0] != '\0', "no sessions found in %s", SESSIONS_DIR);

    snprintf(out, out_len, "%s/%s", SESSIONS_DIR, newest);
    return ESP_OK;
}

static esp_err_t verify_segment(const char *path, size_t expected_bytes, OpusDecoder *decoder, int *out_frames) {
    struct stat info;
    CHECK(stat(path, &info) == 0, "stat failed for %s", path);
    CHECK((size_t)info.st_size == expected_bytes, "%s is %ld bytes, manifest says %u",
          path, (long)info.st_size, (unsigned)expected_bytes);

    FILE *file = fopen(path, "rb");
    CHECK(file != NULL, "could not open %s", path);

    static uint8_t packet[OPUS_MAX_PACKET_BYTES];
    static int16_t pcm[OPUS_FRAME_SIZE_SAMPLES];

    int frames = 0;
    int nonzero_frames = 0;
    esp_err_t status = ESP_OK;

    while (true) {
        uint16_t length = 0;
        if (fread(&length, sizeof(length), 1, file) != 1) {
            break;
        }

        if (length == 0 || length > OPUS_MAX_PACKET_BYTES) {
            ESP_LOGE(TAG, "%s frame %d has implausible length %u", path, frames, (unsigned)length);
            status = ESP_FAIL;
            break;
        }

        if (fread(packet, 1, length, file) != length) {
            ESP_LOGW(TAG, "%s frame %d truncated (expected after a crash)", path, frames);
            break;
        }

        int decoded = opus_decode(decoder, packet, length, pcm, OPUS_FRAME_SIZE_SAMPLES, 0);
        if (decoded != OPUS_FRAME_SIZE_SAMPLES) {
            ESP_LOGE(TAG, "%s frame %d decoded %d samples", path, frames, decoded);
            status = ESP_FAIL;
            break;
        }

        for (int i = 0; i < decoded; i++) {
            if (pcm[i] != 0) {
                nonzero_frames++;
                break;
            }
        }

        frames++;
    }

    fclose(file);

    if (status == ESP_OK) {
        CHECK(nonzero_frames > frames / 2, "%s: only %d of %d frames contain audio", path, nonzero_frames, frames);
    }

    *out_frames = frames;
    return status;
}

esp_err_t self_test_latest_session(void) {
    char session_dir[PATH_LEN];
    if (find_latest_session(session_dir, sizeof(session_dir)) != ESP_OK) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "session: verifying %s", session_dir);

    char manifest_path[PATH_LEN];
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.bin", session_dir);

    manifest_t manifest;
    esp_err_t status = manifest_open(&manifest, manifest_path);
    CHECK(status == ESP_OK, "manifest_open returned %d", status);

    ESP_LOGI(TAG, "session: %d segments, complete=%d", manifest.num_segments, manifest.complete);

    int opus_error = OPUS_OK;
    OpusDecoder *decoder = opus_decoder_create(MICROPHONE_SAMPLE_RATE_HZ, 1, &opus_error);
    CHECK(decoder != NULL && opus_error == OPUS_OK, "opus_decoder_create returned %d", opus_error);

    int total_frames = 0;
    esp_err_t result = ESP_OK;

    for (int i = 0; i < manifest.num_segments; i++) {
        char filename[32];
        manifest_segment_filename(i, filename, sizeof(filename));

        char path[PATH_LEN];
        snprintf(path, sizeof(path), "%s/%s", session_dir, filename);

        int frames = 0;
        if (verify_segment(path, manifest.segments[i].num_bytes, decoder, &frames) != ESP_OK) {
            result = ESP_FAIL;
            break;
        }

        ESP_LOGI(TAG, "  %s: %u bytes, %d frames, %.1f s", filename, (unsigned)manifest.segments[i].num_bytes,
                 frames, frames * 0.02f);

        total_frames += frames;
    }

    opus_decoder_destroy(decoder);

    if (result == ESP_OK) {
        ESP_LOGI(TAG, "session: %d frames total, %.1f s of audio", total_frames, total_frames * 0.02f);
        ESP_LOGI(TAG, "session: PASS");
    }

    return result;
}

esp_err_t self_test_run_all(void) {
    esp_err_t manifest_status = self_test_manifest();
    esp_err_t session_status = self_test_latest_session();

    if (manifest_status == ESP_OK && session_status == ESP_OK) {
        ESP_LOGI(TAG, "ALL TESTS PASSED");
        return ESP_OK;
    }

    ESP_LOGE(TAG, "TESTS FAILED (manifest=%d session=%d)", manifest_status, session_status);
    return ESP_FAIL;
}