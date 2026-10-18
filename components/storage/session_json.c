#include "session_json.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include "audio_format.h"
#include "segment_format.h"

typedef struct {
    char *out;
    size_t max_bytes;
    size_t used;
    bool overflowed;
} writer_t;

__attribute__((format(printf, 2, 3)))
static void append(writer_t *writer, const char *format, ...) {
    if (writer->overflowed) {
        return;
    }

    va_list args;
    va_start(args, format);
    int written = vsnprintf(writer->out + writer->used, writer->max_bytes - writer->used, format, args);
    va_end(args);

    if (written < 0 || (size_t)written >= writer->max_bytes - writer->used) {
        writer->overflowed = true;
        return;
    }
    writer->used += (size_t)written;
}

static void append_string(writer_t *writer, const char *name, const char *value) {
    append(writer, "  \"%s\": \"", name);

    for (const char *c = value; *c != '\0'; c++) {
        unsigned char ch = (unsigned char)*c;
        if (ch == '"' || ch == '\\') {
            append(writer, "\\%c", ch);
        } else if (ch < 0x20) {
            append(writer, "\\u%04x", ch);
        } else {
            append(writer, "%c", ch);
        }
    }

    append(writer, "\",\n");
}

esp_err_t session_json_format(const session_json_t *session, char *out, size_t max_bytes, size_t *out_num_bytes) {
    *out_num_bytes = 0;
    if (max_bytes == 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    writer_t writer = { .out = out, .max_bytes = max_bytes };

    append(&writer, "{\n");
    append_string(&writer, "session_id", session->session_id);
    append_string(&writer, "device_id", session->device_id);
    append(&writer, "  \"started_unix\": %" PRId64 ",\n", session->started_unix);
    append(&writer, "  \"ended_unix\": %" PRId64 ",\n", session->ended_unix);
    append(&writer, "  \"segment_count\": %d,\n", session->segment_count);
    append(&writer, "  \"sample_rate\": %d,\n", AUDIO_FORMAT_SAMPLE_RATE_HZ);
    append(&writer, "  \"channels\": %d,\n", AUDIO_FORMAT_CHANNELS);
    append_string(&writer, "codec", "opus");
    append(&writer, "  \"bitrate\": %" PRIu32 ",\n", session->bitrate);
    append_string(&writer, "opus_application", session->opus_application);
    append_string(&writer, "encryption", "aes-256-gcm");
    append(&writer, "  \"chunk_frames\": %d,\n", SEGMENT_FORMAT_FRAMES_PER_CHUNK);
    append_string(&writer, "firmware_version", session->firmware_version);
    append(&writer, "  \"time_reliable\": %s,\n", session->time_reliable ? "true" : "false");
    append(&writer, "  \"resumed\": %s,\n", session->resume_count > 0 ? "true" : "false");
    append(&writer, "  \"resume_count\": %d,\n", session->resume_count);
    append(&writer, "  \"recovered\": %s,\n", session->recovered ? "true" : "false");
    append(&writer, "  \"muted_ms\": %" PRIu32 ",\n", session->muted_frames * AUDIO_FORMAT_FRAME_MS);
    append(&writer, "  \"health\": {\n");
    append(&writer, "    \"buffer_overflows\": %" PRIu32 ",\n", session->buffer_overflows);
    append(&writer, "    \"sd_write_errors\": %" PRIu32 ",\n", session->sd_write_errors);
    append(&writer, "    \"upload_retries\": %" PRIu32 ",\n", session->upload_retries);
    append(&writer, "    \"wifi_disconnects\": %" PRIu32 "\n", session->wifi_disconnects);
    append(&writer, "  }\n");
    append(&writer, "}\n");

    if (writer.overflowed) {
        out[0] = '\0';
        return ESP_ERR_INVALID_SIZE;
    }

    *out_num_bytes = writer.used;
    return ESP_OK;
}
