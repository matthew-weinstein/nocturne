#include "segment_format.h"

#include <string.h>

void segment_format_header(uint8_t out[SEGMENT_FORMAT_HEADER_BYTES]) {
    memset(out, 0, SEGMENT_FORMAT_HEADER_BYTES);
    memcpy(out, SEGMENT_FORMAT_MAGIC, 4);
    out[4] = SEGMENT_FORMAT_VERSION;
    out[5] = SEGMENT_FORMAT_FRAMES_PER_CHUNK;
}

esp_err_t segment_format_append_frame(uint8_t *chunk, size_t max_bytes, size_t *used_bytes,
                                      const uint8_t *packet, size_t num_bytes) {
    if (num_bytes == 0 || num_bytes > AUDIO_FORMAT_MAX_PACKET_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }
    
    if (*used_bytes + SEGMENT_FORMAT_FRAME_LENGTH_BYTES + num_bytes > max_bytes) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t *frame = chunk + *used_bytes;
    frame[0] = (uint8_t)(num_bytes & 0xFF);
    frame[1] = (uint8_t)(num_bytes >> 8);
    memcpy(frame + SEGMENT_FORMAT_FRAME_LENGTH_BYTES, packet, num_bytes);

    *used_bytes += SEGMENT_FORMAT_FRAME_LENGTH_BYTES + num_bytes;
    return ESP_OK;
}
