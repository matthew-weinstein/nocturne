#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "aes_gcm.h"
#include "audio_format.h"

#define SEGMENT_FORMAT_MAGIC              "NCT2"
#define SEGMENT_FORMAT_VERSION            1
#define SEGMENT_FORMAT_HEADER_BYTES       8
#define SEGMENT_FORMAT_FRAMES_PER_CHUNK   50
#define SEGMENT_FORMAT_FRAME_LENGTH_BYTES 2

#define SEGMENT_FORMAT_MAX_CHUNK_PLAINTEXT_BYTES \
    (SEGMENT_FORMAT_FRAMES_PER_CHUNK * (SEGMENT_FORMAT_FRAME_LENGTH_BYTES + AUDIO_FORMAT_MAX_PACKET_BYTES))
#define SEGMENT_FORMAT_MAX_CHUNK_RECORD_BYTES \
    (SEGMENT_FORMAT_MAX_CHUNK_PLAINTEXT_BYTES + AES_GCM_RECORD_OVERHEAD_BYTES)

void segment_format_header(uint8_t out[SEGMENT_FORMAT_HEADER_BYTES]);
esp_err_t segment_format_append_frame(uint8_t *chunk, size_t max_bytes, size_t *used_bytes,
                                      const uint8_t *packet, size_t num_bytes);
