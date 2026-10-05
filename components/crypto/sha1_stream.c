#include "sha1_stream.h"

#include <stdbool.h>
#include <stdio.h>

#include "esp_log.h"
#include "psa/crypto.h"

#define TAG "sha1_stream"

#define READ_BYTES 4096

static uint8_t read_buffer[READ_BYTES];

static void format_hex(const uint8_t *digest, char *out_hex) {
    static const char digits[] = "0123456789abcdef";

    for (size_t i = 0; i < SHA1_STREAM_DIGEST_BYTES; i++) {
        out_hex[i * 2] = digits[digest[i] >> 4];
        out_hex[i * 2 + 1] = digits[digest[i] & 0x0f];
    }

    out_hex[SHA1_STREAM_DIGEST_BYTES * 2] = '\0';
}

esp_err_t sha1_stream_file(const char *path, char *out_hex, size_t max_hex, size_t *out_num_bytes) {
    if (path == NULL || out_hex == NULL || max_hex < SHA1_STREAM_HEX_LEN || out_num_bytes == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "could not open %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
    psa_status_t status = psa_hash_setup(&operation, PSA_ALG_SHA_1);
    size_t num_bytes = 0;

    while (status == PSA_SUCCESS) {
        size_t num_read = fread(read_buffer, 1, sizeof(read_buffer), file);
        if (num_read == 0) {
            break;
        }

        status = psa_hash_update(&operation, read_buffer, num_read);
        num_bytes += num_read;
    }

    bool read_failed = ferror(file) != 0;
    fclose(file);

    if (read_failed) {
        ESP_LOGE(TAG, "read error in %s after %u bytes", path, (unsigned)num_bytes);
        psa_hash_abort(&operation);
        return ESP_FAIL;
    }

    uint8_t digest[SHA1_STREAM_DIGEST_BYTES];
    size_t digest_bytes = 0;

    if (status == PSA_SUCCESS) {
        status = psa_hash_finish(&operation, digest, sizeof(digest), &digest_bytes);
    }
    if (status != PSA_SUCCESS || digest_bytes != SHA1_STREAM_DIGEST_BYTES) {
        ESP_LOGE(TAG, "hashing %s failed with PSA status %d", path, (int)status);
        psa_hash_abort(&operation);
        return ESP_FAIL;
    }

    format_hex(digest, out_hex);
    *out_num_bytes = num_bytes;
    return ESP_OK;
}
