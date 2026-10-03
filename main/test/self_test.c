#include "self_test.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "opus.h"

#include "aes_gcm.h"
#include "audio_format.h"
#include "key_manager.h"
#include "manifest.h"
#include "sd_card.h"
#include "segment_format.h"
#include "session.h"
#include "sha1_stream.h"
#include "supervisor.h"
#include "secrets.h"

#define TAG "selftest"

#define TEST_MANIFEST_PATH SD_CARD_MOUNT_POINT "/test_manifest.bin"

#define PATH_LEN 128
#define KEY_DERIVATION_TIMEOUT_MS 30000

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

/* ---------- key derivation ---------- */

/* PBKDF2-HMAC-SHA256, 100000 iterations, produced by Python hashlib, the same
   implementation the PC pipeline will use to decrypt. */
static const char KAT_PASSPHRASE[] = "nocturne-test-passphrase";
static const uint8_t KAT_SALT[KEY_MANAGER_SALT_BYTES] = {
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 'n', 'o', 'c', 't', 'u', 'r', 'n', 'e'
};
static const uint8_t KAT_KEY[KEY_MANAGER_KEY_BYTES] = {
    0x87, 0x20, 0xff, 0xc8, 0x79, 0x77, 0xb5, 0xad, 0x1a, 0xc6, 0xf5, 0x31, 0x2e, 0x4d, 0x24, 0x10,
    0x0c, 0x2f, 0x50, 0x66, 0xb8, 0xd9, 0xa3, 0x1a, 0x45, 0x4a, 0x31, 0xd0, 0xcd, 0x33, 0x1b, 0x36
};

esp_err_t self_test_key_derivation(void) {
    uint8_t salt[KEY_MANAGER_SALT_BYTES];
    CHECK(key_manager_device_salt(salt, sizeof(salt)) == ESP_OK, "key_manager_device_salt failed");
    CHECK(memcmp(salt + 6, "nocturne", 8) == 0, "salt does not end in \"nocturne\"");

    uint8_t mac[6];
    CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK, "esp_read_mac failed");
    CHECK(memcmp(salt, mac, sizeof(mac)) == 0, "salt does not start with the device MAC");

    ESP_LOGI(TAG, "key: deriving known-answer vector (100000 iterations)");
    uint8_t key[KEY_MANAGER_KEY_BYTES];
    CHECK(key_manager_derive(KAT_PASSPHRASE, KAT_SALT, sizeof(KAT_SALT), key, sizeof(key)) == ESP_OK,
          "key_manager_derive failed");
    CHECK(memcmp(key, KAT_KEY, sizeof(key)) == 0, "derived key does not match the host vector");

    ESP_LOGI(TAG, "key: deriving with a different passphrase");
    uint8_t other_key[KEY_MANAGER_KEY_BYTES];
    CHECK(key_manager_derive("nocturne-other-passphrase", KAT_SALT, sizeof(KAT_SALT),
                             other_key, sizeof(other_key)) == ESP_OK, "second derive failed");
    CHECK(memcmp(key, other_key, sizeof(key)) != 0, "a different passphrase produced the same key");

    CHECK(key_manager_derive("", KAT_SALT, sizeof(KAT_SALT), key, sizeof(key)) == ESP_ERR_INVALID_ARG,
          "an empty passphrase was accepted");

    ESP_LOGI(TAG, "key: deriving the device key");
    CHECK(key_manager_init(NOCTURNE_PASSPHRASE) == ESP_OK, "key_manager_init failed");
    CHECK(key_manager_wait_ready(KEY_DERIVATION_TIMEOUT_MS) == ESP_OK, "device key not ready in time");
    const uint8_t *device_key = key_manager_key();
    CHECK(device_key != NULL, "key_manager_key returned NULL after init");

    uint8_t expected[KEY_MANAGER_KEY_BYTES];
    CHECK(key_manager_derive(NOCTURNE_PASSPHRASE, salt, sizeof(salt), expected, sizeof(expected)) == ESP_OK,
          "device key re-derive failed");
    CHECK(memcmp(device_key, expected, sizeof(expected)) == 0, "device key is not reproducible");

    ESP_LOGI(TAG, "key: PASS");
    return ESP_OK;
}

/* ---------- chunk encryption ---------- */

/* The record this seals is decrypted by tools/decrypt_chunk.py, which rebuilds
   the same plaintext and derives the same key from KAT_PASSPHRASE. */
#define TEST_CHUNK_SEGMENT_INDEX 42
#define TEST_CHUNK_INDEX         7
#define TEST_CHUNK_FRAMES        50
#define TEST_CHUNK_FRAME_BYTES   46
#define TEST_CHUNK_PLAIN_BYTES   (TEST_CHUNK_FRAMES * (2 + TEST_CHUNK_FRAME_BYTES))
#define TEST_CHUNK_RECORD_BYTES  (TEST_CHUNK_PLAIN_BYTES + AES_GCM_RECORD_OVERHEAD_BYTES)
#define TEST_CHUNK_PATH          SD_CARD_MOUNT_POINT "/test_chunk.bin"

#define NONCE_OFFSET            4
#define CIPHERTEXT_OFFSET       (NONCE_OFFSET + AES_GCM_NONCE_BYTES)
#define CIPHERTEXT_SAMPLE_BYTES 16
#define HEX_LINE_BYTES          32

static uint8_t chunk_plain[TEST_CHUNK_PLAIN_BYTES];
static uint8_t chunk_record[TEST_CHUNK_RECORD_BYTES];
static uint8_t chunk_opened[TEST_CHUNK_PLAIN_BYTES];

static void build_test_chunk(uint8_t *out) {
    size_t offset = 0;

    for (int frame = 0; frame < TEST_CHUNK_FRAMES; frame++) {
        out[offset++] = (uint8_t)TEST_CHUNK_FRAME_BYTES;
        out[offset++] = (uint8_t)(TEST_CHUNK_FRAME_BYTES >> 8);

        for (int i = 0; i < TEST_CHUNK_FRAME_BYTES; i++) {
            out[offset++] = (uint8_t)(frame * 31 + i * 7 + 11);
        }
    }
}

static void log_hex(const uint8_t *bytes, size_t num_bytes) {
    char line[HEX_LINE_BYTES * 2 + 1];

    for (size_t offset = 0; offset < num_bytes; offset += HEX_LINE_BYTES) {
        size_t run = num_bytes - offset < HEX_LINE_BYTES ? num_bytes - offset : HEX_LINE_BYTES;

        for (size_t i = 0; i < run; i++) {
            snprintf(line + i * 2, 3, "%02x", bytes[offset + i]);
        }

        printf("%s\n", line);
    }
}

static esp_err_t seal_test_chunk(size_t *out_record_bytes) {
    esp_err_t status = aes_gcm_seal_chunk(KAT_KEY, TEST_CHUNK_SEGMENT_INDEX, TEST_CHUNK_INDEX,
                                          chunk_plain, sizeof(chunk_plain),
                                          chunk_record, sizeof(chunk_record), out_record_bytes);
    CHECK(status == ESP_OK, "aes_gcm_seal_chunk returned %d", status);
    CHECK(*out_record_bytes == TEST_CHUNK_RECORD_BYTES, "record is %u bytes, expected %u",
          (unsigned)*out_record_bytes, (unsigned)TEST_CHUNK_RECORD_BYTES);
    return ESP_OK;
}

static esp_err_t open_test_chunk(uint16_t segment_index, uint32_t chunk_index, size_t num_record_bytes,
                                 esp_err_t *out_status) {
    size_t plain_bytes = 0;
    size_t record_bytes = 0;

    *out_status = aes_gcm_open_chunk(KAT_KEY, segment_index, chunk_index,
                                     chunk_record, num_record_bytes,
                                     chunk_opened, sizeof(chunk_opened), &plain_bytes, &record_bytes);

    if (*out_status == ESP_OK) {
        CHECK(plain_bytes == sizeof(chunk_plain), "opened %u bytes, expected %u",
              (unsigned)plain_bytes, (unsigned)sizeof(chunk_plain));
        CHECK(record_bytes == TEST_CHUNK_RECORD_BYTES, "consumed %u bytes, expected %u",
              (unsigned)record_bytes, (unsigned)TEST_CHUNK_RECORD_BYTES);
        CHECK(memcmp(chunk_opened, chunk_plain, sizeof(chunk_plain)) == 0,
              "the round trip is not byte-identical");
    }

    return ESP_OK;
}

static esp_err_t write_test_chunk(size_t num_bytes) {
    FILE *file = fopen(TEST_CHUNK_PATH, "wb");
    CHECK(file != NULL, "could not open " TEST_CHUNK_PATH);

    size_t written = fwrite(chunk_record, 1, num_bytes, file);
    fflush(file);
    fclose(file);

    CHECK(written == num_bytes, "wrote %u of %u bytes", (unsigned)written, (unsigned)num_bytes);
    return ESP_OK;
}

esp_err_t self_test_chunk_crypto(void) {
    build_test_chunk(chunk_plain);

    ESP_LOGI(TAG, "chunk: sealing %u bytes as segment %d chunk %d",
             (unsigned)sizeof(chunk_plain), TEST_CHUNK_SEGMENT_INDEX, TEST_CHUNK_INDEX);

    size_t record_bytes = 0;
    if (seal_test_chunk(&record_bytes) != ESP_OK) {
        return ESP_FAIL;
    }

    uint8_t first_nonce[AES_GCM_NONCE_BYTES];
    uint8_t first_ciphertext[CIPHERTEXT_SAMPLE_BYTES];
    memcpy(first_nonce, chunk_record + NONCE_OFFSET, sizeof(first_nonce));
    memcpy(first_ciphertext, chunk_record + CIPHERTEXT_OFFSET, sizeof(first_ciphertext));

    ESP_LOGI(TAG, "chunk: sealing again, the nonce must be fresh");
    if (seal_test_chunk(&record_bytes) != ESP_OK) {
        return ESP_FAIL;
    }
    CHECK(memcmp(chunk_record + NONCE_OFFSET, first_nonce, sizeof(first_nonce)) != 0, "the nonce repeated");
    CHECK(memcmp(chunk_record + CIPHERTEXT_OFFSET, first_ciphertext, sizeof(first_ciphertext)) != 0,
          "the same plaintext sealed to the same ciphertext");

    uint32_t declared = (uint32_t)chunk_record[0] | ((uint32_t)chunk_record[1] << 8) |
                        ((uint32_t)chunk_record[2] << 16) | ((uint32_t)chunk_record[3] << 24);
    CHECK(declared == sizeof(chunk_plain), "the length field is %u, expected %u",
          (unsigned)declared, (unsigned)sizeof(chunk_plain));
    CHECK(memcmp(chunk_record + CIPHERTEXT_OFFSET, chunk_plain, CIPHERTEXT_SAMPLE_BYTES) != 0,
          "the chunk is not encrypted");

    ESP_LOGI(TAG, "chunk: opening with the matching indices");
    esp_err_t status = ESP_FAIL;
    if (open_test_chunk(TEST_CHUNK_SEGMENT_INDEX, TEST_CHUNK_INDEX, record_bytes, &status) != ESP_OK) {
        return ESP_FAIL;
    }
    CHECK(status == ESP_OK, "aes_gcm_open_chunk returned %d", status);

    ESP_LOGI(TAG, "chunk: the AAD must bind the segment and chunk index");
    if (open_test_chunk(TEST_CHUNK_SEGMENT_INDEX + 1, TEST_CHUNK_INDEX, record_bytes, &status) != ESP_OK) {
        return ESP_FAIL;
    }
    CHECK(status == ESP_ERR_INVALID_CRC, "a wrong segment index returned %d", status);

    if (open_test_chunk(TEST_CHUNK_SEGMENT_INDEX, TEST_CHUNK_INDEX + 1, record_bytes, &status) != ESP_OK) {
        return ESP_FAIL;
    }
    CHECK(status == ESP_ERR_INVALID_CRC, "a wrong chunk index returned %d", status);

    ESP_LOGI(TAG, "chunk: a flipped ciphertext bit must fail the tag");
    chunk_record[CIPHERTEXT_OFFSET] ^= 0x01;
    if (open_test_chunk(TEST_CHUNK_SEGMENT_INDEX, TEST_CHUNK_INDEX, record_bytes, &status) != ESP_OK) {
        return ESP_FAIL;
    }
    CHECK(status == ESP_ERR_INVALID_CRC, "a tampered chunk returned %d", status);
    chunk_record[CIPHERTEXT_OFFSET] ^= 0x01;

    ESP_LOGI(TAG, "chunk: a record running past the end must be rejected");
    if (open_test_chunk(TEST_CHUNK_SEGMENT_INDEX, TEST_CHUNK_INDEX, record_bytes - 1, &status) != ESP_OK) {
        return ESP_FAIL;
    }
    CHECK(status == ESP_ERR_INVALID_SIZE, "a truncated record returned %d", status);

    if (open_test_chunk(TEST_CHUNK_SEGMENT_INDEX, TEST_CHUNK_INDEX, record_bytes, &status) != ESP_OK) {
        return ESP_FAIL;
    }
    CHECK(status == ESP_OK, "the restored record no longer opens (%d)", status);

    if (write_test_chunk(record_bytes) != ESP_OK) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "chunk: wrote %u bytes to %s", (unsigned)record_bytes, TEST_CHUNK_PATH);

    printf("---- nocturne chunk record begin ----\n");
    log_hex(chunk_record, record_bytes);
    printf("---- nocturne chunk record end ----\n");

    ESP_LOGI(TAG, "chunk: PASS");
    return ESP_OK;
}

/* ---------- streaming SHA1 ---------- */

/* Digests produced by Python hashlib. The pattern spans several read buffers
   and ends partway through one. */
#define TEST_SHA1_PATH  SD_CARD_MOUNT_POINT "/test_sha1.bin"
#define TEST_SHA1_BYTES 10000

static const char TEST_SHA1_HEX[] = "504bab9f255da75e2c3c08dfbc11061a05996bbf";
static const char EMPTY_SHA1_HEX[] = "da39a3ee5e6b4b0d3255bfef95601890afd80709";

static esp_err_t write_sha1_pattern(size_t num_bytes) {
    FILE *file = fopen(TEST_SHA1_PATH, "wb");
    CHECK(file != NULL, "could not open " TEST_SHA1_PATH);

    size_t written = 0;
    for (size_t i = 0; i < num_bytes; i++) {
        if (fputc((uint8_t)(i * 7 + 3), file) == EOF) {
            break;
        }
        written++;
    }
    fclose(file);

    CHECK(written == num_bytes, "wrote %u of %u bytes", (unsigned)written, (unsigned)num_bytes);
    return ESP_OK;
}

static esp_err_t verify_sha1(size_t num_bytes, const char *expected_hex) {
    if (write_sha1_pattern(num_bytes) != ESP_OK) {
        return ESP_FAIL;
    }

    char hex[SHA1_STREAM_HEX_LEN];
    size_t hashed_bytes = 0;
    esp_err_t status = sha1_stream_file(TEST_SHA1_PATH, hex, sizeof(hex), &hashed_bytes);
    CHECK(status == ESP_OK, "sha1_stream_file returned %d", status);
    CHECK(hashed_bytes == num_bytes, "hashed %u bytes, expected %u", (unsigned)hashed_bytes, (unsigned)num_bytes);
    CHECK(strcmp(hex, expected_hex) == 0, "digest %s, expected %s", hex, expected_hex);
    return ESP_OK;
}

esp_err_t self_test_sha1_stream(void) {
    ESP_LOGI(TAG, "sha1: hashing a %d-byte known file", TEST_SHA1_BYTES);
    if (verify_sha1(TEST_SHA1_BYTES, TEST_SHA1_HEX) != ESP_OK) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "sha1: hashing an empty file");
    if (verify_sha1(0, EMPTY_SHA1_HEX) != ESP_OK) {
        return ESP_FAIL;
    }
    remove(TEST_SHA1_PATH);

    char hex[SHA1_STREAM_HEX_LEN];
    size_t hashed_bytes = 0;
    CHECK(sha1_stream_file(TEST_SHA1_PATH, hex, sizeof(hex), &hashed_bytes) == ESP_ERR_NOT_FOUND,
          "a missing file was hashed");
    CHECK(sha1_stream_file(TEST_SHA1_PATH, hex, sizeof(hex) - 1, &hashed_bytes) == ESP_ERR_INVALID_ARG,
          "a short digest buffer was accepted");

    ESP_LOGI(TAG, "sha1: PASS");
    return ESP_OK;
}

/* ---------- session verification ---------- */

static esp_err_t verify_segment(const char *path, uint16_t segment_index, size_t expected_bytes,
                                OpusDecoder *decoder, int *out_frames) {
    struct stat info;
    CHECK(stat(path, &info) == 0, "stat failed for %s", path);
    CHECK((size_t)info.st_size == expected_bytes, "%s is %ld bytes, manifest says %u",
          path, (long)info.st_size, (unsigned)expected_bytes);

    FILE *file = fopen(path, "rb");
    CHECK(file != NULL, "could not open %s", path);

    uint8_t header[SEGMENT_FORMAT_HEADER_BYTES];
    if (fread(header, 1, sizeof(header), file) != sizeof(header) || memcmp(header, SEGMENT_FORMAT_MAGIC, 4) != 0 ||
        header[4] != SEGMENT_FORMAT_VERSION || header[5] != SEGMENT_FORMAT_FRAMES_PER_CHUNK) {
        ESP_LOGE(TAG, "%s has a bad segment header", path);
        fclose(file);
        return ESP_FAIL;
    }

    static uint8_t record[SEGMENT_FORMAT_MAX_CHUNK_RECORD_BYTES];
    static uint8_t plaintext[SEGMENT_FORMAT_MAX_CHUNK_PLAINTEXT_BYTES];
    static int16_t pcm[AUDIO_FORMAT_FRAME_SAMPLES];

    int frames = 0;
    int nonzero_frames = 0;
    uint32_t chunk_index = 0;
    esp_err_t status = ESP_OK;

    while (status == ESP_OK) {
        if (fread(record, 1, 4, file) != 4) {
            break;
        }

        uint32_t payload_bytes = (uint32_t)record[0] | ((uint32_t)record[1] << 8) |
                                 ((uint32_t)record[2] << 16) | ((uint32_t)record[3] << 24);
        if (payload_bytes == 0 || payload_bytes > SEGMENT_FORMAT_MAX_CHUNK_PLAINTEXT_BYTES) {
            ESP_LOGE(TAG, "%s chunk %u has implausible length %u", path, (unsigned)chunk_index,
                     (unsigned)payload_bytes);
            status = ESP_FAIL;
            break;
        }

        size_t num_record_bytes = payload_bytes + AES_GCM_RECORD_OVERHEAD_BYTES;
        if (fread(record + 4, 1, num_record_bytes - 4, file) != num_record_bytes - 4) {
            ESP_LOGW(TAG, "%s chunk %u truncated (expected after a crash)", path, (unsigned)chunk_index);
            break;
        }

        size_t num_plaintext_bytes = 0;
        size_t consumed_bytes = 0;
        esp_err_t open_status = aes_gcm_open_chunk(key_manager_key(), segment_index, chunk_index,
                                                   record, num_record_bytes, plaintext, sizeof(plaintext),
                                                   &num_plaintext_bytes, &consumed_bytes);
        if (open_status != ESP_OK) {
            ESP_LOGE(TAG, "%s chunk %u failed to open (%d)", path, (unsigned)chunk_index, open_status);
            status = ESP_FAIL;
            break;
        }

        size_t offset = 0;
        while (offset + 2 <= num_plaintext_bytes) {
            uint16_t length = (uint16_t)(plaintext[offset] | (plaintext[offset + 1] << 8));
            offset += 2;

            if (length == 0 || length > AUDIO_FORMAT_MAX_PACKET_BYTES || offset + length > num_plaintext_bytes) {
                ESP_LOGE(TAG, "%s frame %d has implausible length %u", path, frames, (unsigned)length);
                status = ESP_FAIL;
                break;
            }

            int decoded = opus_decode(decoder, plaintext + offset, length, pcm, AUDIO_FORMAT_FRAME_SAMPLES, 0);
            if (decoded != AUDIO_FORMAT_FRAME_SAMPLES) {
                ESP_LOGE(TAG, "%s frame %d decoded %d samples", path, frames, decoded);
                status = ESP_FAIL;
                break;
            }
            offset += length;

            for (int i = 0; i < decoded; i++) {
                if (pcm[i] != 0) {
                    nonzero_frames++;
                    break;
                }
            }

            frames++;
        }

        chunk_index++;
    }

    fclose(file);

    if (status == ESP_OK) {
        CHECK(nonzero_frames > frames / 2, "%s: only %d of %d frames contain audio", path, nonzero_frames, frames);
    }

    *out_frames = frames;
    return status;
}

esp_err_t self_test_current_session(void) {
    const char *session_dir = session_current_dir();
    CHECK(session_dir[0] != '\0', "no session has been started");
    ESP_LOGI(TAG, "session: verifying %s", session_dir);

    char manifest_path[PATH_LEN];
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.bin", session_dir);

    manifest_t manifest;
    esp_err_t status = manifest_open(&manifest, manifest_path);
    CHECK(status == ESP_OK, "manifest_open returned %d", status);

    ESP_LOGI(TAG, "session: %d segments, complete=%d", manifest.num_segments, manifest.complete);

    int opus_error = OPUS_OK;
    OpusDecoder *decoder = opus_decoder_create(AUDIO_FORMAT_SAMPLE_RATE_HZ, 1, &opus_error);
    CHECK(decoder != NULL && opus_error == OPUS_OK, "opus_decoder_create returned %d", opus_error);

    int total_frames = 0;
    esp_err_t result = ESP_OK;

    for (int i = 0; i < manifest.num_segments; i++) {
        char filename[32];
        manifest_segment_filename(i, filename, sizeof(filename));

        char path[PATH_LEN];
        snprintf(path, sizeof(path), "%s/%s", session_dir, filename);

        int frames = 0;
        if (verify_segment(path, (uint16_t)i, manifest.segments[i].num_bytes, decoder, &frames) != ESP_OK) {
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

/* ---------- supervisor transitions and LED ---------- */

#define SUPERVISOR_STEP_MS 2000

typedef struct {
    supervisor_event_t event;
    supervisor_state_t expected;
} supervisor_step_t;

static const supervisor_step_t supervisor_walk[] = {
    {SUPERVISOR_EVENT_RESUME_FOUND,    SUPERVISOR_STATE_RESUMING},
    {SUPERVISOR_EVENT_SHORT_PRESS,     SUPERVISOR_STATE_RESUMING},
    {SUPERVISOR_EVENT_RESUMED,         SUPERVISOR_STATE_RECORDING},
    {SUPERVISOR_EVENT_SHORT_PRESS,     SUPERVISOR_STATE_RECORDING},
    {SUPERVISOR_EVENT_VERY_LONG_PRESS, SUPERVISOR_STATE_RECORDING},
    {SUPERVISOR_EVENT_LONG_PRESS,      SUPERVISOR_STATE_FINALIZING},
    {SUPERVISOR_EVENT_SHORT_PRESS,     SUPERVISOR_STATE_FINALIZING},
    {SUPERVISOR_EVENT_FINALIZE_OK,     SUPERVISOR_STATE_IDLE},
    {SUPERVISOR_EVENT_LONG_PRESS,      SUPERVISOR_STATE_IDLE},
    {SUPERVISOR_EVENT_VERY_LONG_PRESS, SUPERVISOR_STATE_IDLE},
    {SUPERVISOR_EVENT_SHORT_PRESS,     SUPERVISOR_STATE_RECORDING},
    {SUPERVISOR_EVENT_LONG_PRESS,      SUPERVISOR_STATE_FINALIZING},
    {SUPERVISOR_EVENT_FINALIZE_FAILED, SUPERVISOR_STATE_IDLE},
};

static esp_err_t supervisor_result = ESP_ERR_NOT_FINISHED;

esp_err_t self_test_supervisor(void) {
    supervisor_result = ESP_FAIL;

    CHECK(supervisor_state() == SUPERVISOR_STATE_BOOT, "supervisor: not in BOOT at start");

    size_t num_steps = sizeof(supervisor_walk) / sizeof(supervisor_walk[0]);
    for (size_t i = 0; i < num_steps; i++) {
        CHECK(supervisor_post(supervisor_walk[i].event) == ESP_OK, "supervisor: step %u post failed",
              (unsigned)i);
        vTaskDelay(pdMS_TO_TICKS(SUPERVISOR_STEP_MS));
        CHECK(supervisor_state() == supervisor_walk[i].expected, "supervisor: step %u reached state %d, expected %d",
              (unsigned)i, supervisor_state(), supervisor_walk[i].expected);
    }

    ESP_LOGI(TAG, "supervisor: PASS");
    supervisor_result = ESP_OK;
    return ESP_OK;
}

esp_err_t self_test_run_all(void) {
    esp_err_t manifest_status = self_test_manifest();
    UBaseType_t priority = uxTaskPriorityGet(NULL);
    vTaskPrioritySet(NULL, tskIDLE_PRIORITY);
    esp_err_t key_status = self_test_key_derivation();
    vTaskPrioritySet(NULL, priority);

    esp_err_t chunk_status = self_test_chunk_crypto();
    esp_err_t sha1_status = self_test_sha1_stream();
    esp_err_t session_status = self_test_current_session();

    if (manifest_status == ESP_OK && key_status == ESP_OK && chunk_status == ESP_OK &&
        sha1_status == ESP_OK && session_status == ESP_OK && supervisor_result == ESP_OK) {
        ESP_LOGI(TAG, "ALL TESTS PASSED");
        return ESP_OK;
    }

    ESP_LOGE(TAG, "TESTS FAILED (manifest=%d key=%d chunk=%d sha1=%d session=%d supervisor=%d)",
             manifest_status, key_status, chunk_status, sha1_status, session_status, supervisor_result);
    return ESP_FAIL;
}