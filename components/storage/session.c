#include "session.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <errno.h>
#include <stdbool.h>
#include <unistd.h>

#include "aes_gcm.h"
#include "key_manager.h"
#include "manifest.h"
#include "recovery.h"
#include "sd_card.h"
#include "segment_cursor.h"
#include "segment_format.h"
#include "session_id.h"
#include "esp_log.h"
#include "sdkconfig.h"

#define MAX_COLLISION_SUFFIX 99

#define SEGMENT_SECONDS CONFIG_NOCTURNE_SEGMENT_SECONDS
#define FRAMES_PER_SEGMENT ((SEGMENT_SECONDS * 1000) / AUDIO_FORMAT_FRAME_MS)

#define PATH_LEN        128

static const char *TAG = "session";

static manifest_t manifest;
static char session_dir[SESSION_DIR_LEN];
static char manifest_path[PATH_LEN];
static session_segment_handler_t segment_closed;

static FILE *segment_file;
static segment_cursor_t cursor;
static size_t bytes_in_segment;

static uint8_t chunk_plaintext[SEGMENT_FORMAT_MAX_CHUNK_PLAINTEXT_BYTES];
static uint8_t chunk_record[SEGMENT_FORMAT_MAX_CHUNK_RECORD_BYTES];
static size_t chunk_plaintext_bytes;
static uint32_t chunk_index;
static uint32_t num_write_errors;

static bool write_at_end(const uint8_t *data, size_t num_bytes) {
    return fseek(segment_file, (long)bytes_in_segment, SEEK_SET) == 0
        && fwrite(data, 1, num_bytes, segment_file) == num_bytes
        && fsync(fileno(segment_file)) == 0;
}

static esp_err_t append_to_segment(const uint8_t *data, size_t num_bytes) {
    for (int retry = 0; !write_at_end(data, num_bytes); retry++) {
        num_write_errors++;
        if (retry == SD_CARD_WRITE_RETRIES) {
            ESP_LOGE(TAG, "segment write failed after %d retries", SD_CARD_WRITE_RETRIES);
            return ESP_FAIL;
        }
        ESP_LOGW(TAG, "segment write failed, retry %d of %d", retry + 1, SD_CARD_WRITE_RETRIES);
        clearerr(segment_file);
        usleep(SD_CARD_RETRY_DELAY_US);
    }

    bytes_in_segment += num_bytes;
    return ESP_OK;
}

static esp_err_t write_segment_header(void) {
    uint8_t header[SEGMENT_FORMAT_HEADER_BYTES];
    segment_format_header(header);
    return append_to_segment(header, sizeof(header));
}

static esp_err_t open_segment(void) {
    char filename[32];
    manifest_segment_filename(manifest.num_segments, filename, sizeof(filename));

    char path[PATH_LEN];
    snprintf(path, sizeof(path), "%s/%s", session_dir, filename);

    segment_file = fopen(path, "wb");
    if (segment_file == NULL) {
        return ESP_FAIL;
    }
    setvbuf(segment_file, NULL, _IONBF, 0);

    segment_cursor_reset(&cursor, FRAMES_PER_SEGMENT);
    chunk_plaintext_bytes = 0;
    chunk_index = 0;
    bytes_in_segment = 0;

    esp_err_t status = write_segment_header();
    if (status != ESP_OK) {
        fclose(segment_file);
        segment_file = NULL;
    }
    return status;
}

static esp_err_t flush_chunk(void) {
    if (chunk_plaintext_bytes == 0) {
        return ESP_OK;
    }

    size_t record_bytes = 0;
    esp_err_t status = aes_gcm_seal_chunk(key_manager_key(), (uint16_t)manifest.num_segments, chunk_index,
                                          chunk_plaintext, chunk_plaintext_bytes,
                                          chunk_record, sizeof(chunk_record), &record_bytes);
    if (status != ESP_OK) {
        return status;
    }

    status = append_to_segment(chunk_record, record_bytes);
    if (status != ESP_OK) {
        return status;
    }

    chunk_index++;
    chunk_plaintext_bytes = 0;
    return ESP_OK;
}

static esp_err_t close_segment(void) {
    if (segment_file == NULL) {
        return ESP_OK;
    }

    esp_err_t status = flush_chunk();

    fclose(segment_file);
    segment_file = NULL;

    if (status != ESP_OK) {
        return status;
    }

    return manifest_record_segment(&manifest, manifest_path, bytes_in_segment);
}

esp_err_t session_start(session_segment_handler_t on_segment_closed) {
    esp_err_t key_status = key_manager_wait_ready();
    if (key_status != ESP_OK) {
        return key_status;
    }

    mkdir(SESSION_ROOT_DIR, 0755);

    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);

    char session_id[MANIFEST_SESSION_ID_LEN];
    session_id_format(&local, 0, session_id, sizeof(session_id));

    for (int suffix = 1; ; suffix++) {
        snprintf(session_dir, sizeof(session_dir), "%s/%s", SESSION_ROOT_DIR, session_id);

        if (mkdir(session_dir, 0755) == 0) {
            break;
        }
        if (errno != EEXIST || suffix > MAX_COLLISION_SUFFIX) {
            return ESP_FAIL;
        }

        session_id_format(&local, suffix, session_id, sizeof(session_id));
    }

    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.bin", session_dir);
    segment_closed = on_segment_closed;
    num_write_errors = 0;

    esp_err_t status = manifest_create(&manifest, manifest_path, session_id, (int64_t)now);
    if (status != ESP_OK) {
        return status;
    }

    return open_segment();
}

esp_err_t session_resume(const char *existing_session_dir, session_segment_handler_t on_segment_closed) {
    esp_err_t key_status = key_manager_wait_ready();
    if (key_status != ESP_OK) {
        return key_status;
    }

    snprintf(session_dir, sizeof(session_dir), "%s", existing_session_dir);
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.bin", session_dir);
    segment_closed = on_segment_closed;
    num_write_errors = 0;

    esp_err_t status = manifest_open(&manifest, manifest_path);
    if (status != ESP_OK) {
        return status;
    }
    if (manifest.complete) {
        return ESP_ERR_INVALID_STATE;
    }

    int64_t open_segment_written_unix = 0;
    status = recovery_record_open_segment(&manifest, session_dir, &open_segment_written_unix);
    if (status != ESP_OK) {
        return status;
    }
    status = manifest_record_resume(&manifest, manifest_path);
    if (status != ESP_OK) {
        return status;
    }
    if (segment_closed != NULL) {
        segment_closed(session_dir);
    }

    return open_segment();
}

esp_err_t session_write_packet(const uint8_t *packet, size_t num_bytes) {
    esp_err_t status = segment_format_append_frame(chunk_plaintext, sizeof(chunk_plaintext),
                                                   &chunk_plaintext_bytes, packet, num_bytes);
    if (status != ESP_OK) {
        return status;
    }

    switch (segment_cursor_add_frame(&cursor)) {
    case SEGMENT_CURSOR_FLUSH_CHUNK:
        return flush_chunk();
    case SEGMENT_CURSOR_CLOSE_SEGMENT:
        status = close_segment();
        if (status != ESP_OK) {
            return status;
        }
        status = manifest_record_heartbeat(manifest_path);
        if (status != ESP_OK) {
            return status;
        }
        if (segment_closed != NULL) {
            segment_closed(session_dir);
        }
        return open_segment();
    case SEGMENT_CURSOR_CONTINUE:
        break;
    }
    return ESP_OK;
}

esp_err_t session_finish(void) {
    esp_err_t status = close_segment();
    if (status != ESP_OK) {
        return status;
    }

    return manifest_record_complete(&manifest, manifest_path);
}

const char *session_current_dir(void) {
    return session_dir;
}

uint32_t session_write_error_count(void) {
    return num_write_errors;
}
