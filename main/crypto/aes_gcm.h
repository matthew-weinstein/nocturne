#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define AES_GCM_KEY_BYTES   32
#define AES_GCM_NONCE_BYTES 12
#define AES_GCM_TAG_BYTES   16

/* A chunk record is num_bytes (uint32 LE) || nonce || ciphertext || tag. */
#define AES_GCM_RECORD_OVERHEAD_BYTES (4 + AES_GCM_NONCE_BYTES + AES_GCM_TAG_BYTES)

esp_err_t aes_gcm_seal_chunk(const uint8_t *key, uint16_t segment_index, uint32_t chunk_index,
                             const uint8_t *plaintext, size_t num_plaintext_bytes,
                             uint8_t *out_record, size_t max_record_bytes, size_t *out_record_bytes);

/* Returns ESP_ERR_INVALID_SIZE for a record that runs past the buffer and
   ESP_ERR_INVALID_CRC for one whose tag fails. A reader stops on either. */
esp_err_t aes_gcm_open_chunk(const uint8_t *key, uint16_t segment_index, uint32_t chunk_index,
                             const uint8_t *record, size_t num_record_bytes,
                             uint8_t *out_plaintext, size_t max_plaintext_bytes,
                             size_t *out_plaintext_bytes, size_t *out_record_bytes);
