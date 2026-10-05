#include "aes_gcm.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "psa/crypto.h"

#define TAG "aes_gcm"

#define LENGTH_FIELD_BYTES 4
#define AAD_BYTES          6

static void write_u32(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)(value);
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

static uint32_t read_u32(const uint8_t *in) {
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

static void build_aad(uint16_t segment_index, uint32_t chunk_index, uint8_t *out) {
    out[0] = (uint8_t)(segment_index);
    out[1] = (uint8_t)(segment_index >> 8);
    write_u32(out + 2, chunk_index);
}

static psa_status_t import_key(const uint8_t *key, mbedtls_svc_key_id_t *out_id) {
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;

    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_GCM);
    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, AES_GCM_KEY_BYTES * 8);

    return psa_import_key(&attributes, key, AES_GCM_KEY_BYTES, out_id);
}

esp_err_t aes_gcm_seal_chunk(const uint8_t *key, uint16_t segment_index, uint32_t chunk_index,
                             const uint8_t *plaintext, size_t num_plaintext_bytes,
                             uint8_t *out_record, size_t max_record_bytes, size_t *out_record_bytes) {
    if (key == NULL || plaintext == NULL || num_plaintext_bytes == 0 ||
        out_record == NULL || out_record_bytes == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t record_bytes = num_plaintext_bytes + AES_GCM_RECORD_OVERHEAD_BYTES;
    if (max_record_bytes < record_bytes) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t *nonce = out_record + LENGTH_FIELD_BYTES;
    uint8_t *ciphertext = nonce + AES_GCM_NONCE_BYTES;
    esp_fill_random(nonce, AES_GCM_NONCE_BYTES);

    uint8_t aad[AAD_BYTES];
    build_aad(segment_index, chunk_index, aad);

    mbedtls_svc_key_id_t key_id = MBEDTLS_SVC_KEY_ID_INIT;
    size_t sealed_bytes = 0;

    psa_status_t status = import_key(key, &key_id);
    if (status == PSA_SUCCESS) {
        status = psa_aead_encrypt(key_id, PSA_ALG_GCM, nonce, AES_GCM_NONCE_BYTES,
                                  aad, sizeof(aad), plaintext, num_plaintext_bytes,
                                  ciphertext, num_plaintext_bytes + AES_GCM_TAG_BYTES, &sealed_bytes);
        psa_destroy_key(key_id);
    }

    if (status != PSA_SUCCESS || sealed_bytes != num_plaintext_bytes + AES_GCM_TAG_BYTES) {
        ESP_LOGE(TAG, "sealing chunk %u of segment %u failed with PSA status %d",
                 (unsigned)chunk_index, (unsigned)segment_index, (int)status);
        return ESP_FAIL;
    }

    write_u32(out_record, (uint32_t)num_plaintext_bytes);
    *out_record_bytes = record_bytes;

    return ESP_OK;
}

esp_err_t aes_gcm_open_chunk(const uint8_t *key, uint16_t segment_index, uint32_t chunk_index,
                             const uint8_t *record, size_t num_record_bytes,
                             uint8_t *out_plaintext, size_t max_plaintext_bytes,
                             size_t *out_plaintext_bytes, size_t *out_record_bytes) {
    if (key == NULL || record == NULL || out_plaintext == NULL ||
        out_plaintext_bytes == NULL || out_record_bytes == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (num_record_bytes < AES_GCM_RECORD_OVERHEAD_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint32_t num_cipher_bytes = read_u32(record);

    if (num_cipher_bytes == 0 || num_cipher_bytes > num_record_bytes - AES_GCM_RECORD_OVERHEAD_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (num_cipher_bytes > max_plaintext_bytes) {
        return ESP_ERR_INVALID_SIZE;
    }

    const uint8_t *nonce = record + LENGTH_FIELD_BYTES;
    const uint8_t *ciphertext = nonce + AES_GCM_NONCE_BYTES;

    uint8_t aad[AAD_BYTES];
    build_aad(segment_index, chunk_index, aad);

    mbedtls_svc_key_id_t key_id = MBEDTLS_SVC_KEY_ID_INIT;
    size_t opened_bytes = 0;

    psa_status_t status = import_key(key, &key_id);
    if (status == PSA_SUCCESS) {
        status = psa_aead_decrypt(key_id, PSA_ALG_GCM, nonce, AES_GCM_NONCE_BYTES,
                                  aad, sizeof(aad), ciphertext, num_cipher_bytes + AES_GCM_TAG_BYTES,
                                  out_plaintext, max_plaintext_bytes, &opened_bytes);
        psa_destroy_key(key_id);
    }

    if (status == PSA_ERROR_INVALID_SIGNATURE) {
        return ESP_ERR_INVALID_CRC;
    }

    if (status != PSA_SUCCESS || opened_bytes != num_cipher_bytes) {
        ESP_LOGE(TAG, "opening chunk %u of segment %u failed with PSA status %d",
                 (unsigned)chunk_index, (unsigned)segment_index, (int)status);
        return ESP_FAIL;
    }

    *out_plaintext_bytes = opened_bytes;
    *out_record_bytes = num_cipher_bytes + AES_GCM_RECORD_OVERHEAD_BYTES;
    
    return ESP_OK;
}
