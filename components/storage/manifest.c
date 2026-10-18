#include "manifest.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sd_card.h"

#define MANIFEST_MAGIC "NCT1"
#define MANIFEST_MAGIC_LEN 4

#define RECORD_SEGMENT_CLOSED 1
#define RECORD_SEGMENT_UPLOADED 2
#define RECORD_SESSION_COMPLETE 3
#define RECORD_HEARTBEAT 4
#define RECORD_SESSION_RESUMED 5
#define RECORD_METADATA_UPLOADED 6
#define RECORD_MUTED 7

typedef struct __attribute__((packed)) {
    char magic[MANIFEST_MAGIC_LEN];
    char session_id[MANIFEST_SESSION_ID_LEN];
    int64_t started_unix;
} manifest_header_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint16_t index;
    uint32_t num_bytes;
} manifest_record_t;

static pthread_mutex_t journal_lock = PTHREAD_MUTEX_INITIALIZER;

void manifest_segment_filename(int index, char *out, size_t out_len) {
    snprintf(out, out_len, "seg_%04d.opus.enc", index);
}

static bool write_file(const char *path, const char *mode, const void *data, size_t num_bytes) {
    FILE *file = fopen(path, mode);
    if (file == NULL) {
        return false;
    }

    bool written = fwrite(data, num_bytes, 1, file) == 1;
    written = fflush(file) == 0 && written;
    return fclose(file) == 0 && written;
}

static esp_err_t write_journal(const char *path, bool append, const void *data, size_t num_bytes) {
    pthread_mutex_lock(&journal_lock);

    off_t length = 0;
    if (append) {
        struct stat info;
        if (stat(path, &info) != 0) {
            pthread_mutex_unlock(&journal_lock);
            return ESP_ERR_NOT_FOUND;
        }
        length = info.st_size;
    }

    esp_err_t status = ESP_FAIL;
    for (int attempt = 0; attempt <= SD_CARD_WRITE_RETRIES; attempt++) {
        if (attempt > 0) {
            usleep(SD_CARD_RETRY_DELAY_US);
            if (append && truncate(path, length) != 0) {
                continue;
            }
        }
        if (write_file(path, append ? "ab" : "wb", data, num_bytes)) {
            status = ESP_OK;
            break;
        }
    }

    pthread_mutex_unlock(&journal_lock);
    return status;
}

static esp_err_t append_record(const char *path, const manifest_record_t *record) {
    return write_journal(path, true, record, sizeof(*record));
}

static void apply_mute(manifest_t *manifest, int segment, uint32_t start_frame, uint32_t num_frames) {
    manifest->muted_frames += num_frames;

    if (manifest->num_mutes > 0) {
        manifest_mute_t *last = &manifest->mutes[manifest->num_mutes - 1];
        if (last->segment == segment && last->start_frame + last->num_frames == start_frame) {
            last->num_frames += num_frames;
            return;
        }
    }

    if (manifest->num_mutes == MANIFEST_MAX_MUTES) {
        manifest->mutes_truncated = true;
        return;
    }

    manifest->mutes[manifest->num_mutes] = (manifest_mute_t){
        .segment = segment,
        .start_frame = start_frame,
        .num_frames = num_frames,
    };
    manifest->num_mutes++;
}

static void apply_record(manifest_t *manifest, const manifest_record_t *record) {
    switch (record->type) {
    case RECORD_SEGMENT_CLOSED:
        if (record->index < MANIFEST_MAX_SEGMENTS) {
            manifest->segments[record->index].num_bytes = record->num_bytes;
            manifest->segments[record->index].uploaded = false;
            if (record->index >= manifest->num_segments) {
                manifest->num_segments = record->index + 1;
            }
        }
        break;
    case RECORD_SEGMENT_UPLOADED:
        if (record->index < MANIFEST_MAX_SEGMENTS) {
            manifest->segments[record->index].uploaded = true;
        }
        break;
    case RECORD_SESSION_COMPLETE:
        manifest->complete = true;
        break;
    case RECORD_SESSION_RESUMED:
        manifest->resume_count++;
        break;
    case RECORD_METADATA_UPLOADED:
        manifest->metadata_uploaded = true;
        break;
    case RECORD_MUTED:
        apply_mute(manifest, record->index, record->num_bytes & MANIFEST_MAX_MUTE_START,
                   record->num_bytes >> MANIFEST_MUTE_START_BITS);
        break;
    default:
        break;
    }
}

esp_err_t manifest_create(manifest_t *manifest, const char *path, const char *session_id, int64_t started_unix) {
    memset(manifest, 0, sizeof(*manifest));
    strncpy(manifest->session_id, session_id, MANIFEST_SESSION_ID_LEN - 1);
    manifest->started_unix = started_unix;

    manifest_header_t header;
    memset(&header, 0, sizeof(header));
    memcpy(header.magic, MANIFEST_MAGIC, MANIFEST_MAGIC_LEN);
    strncpy(header.session_id, session_id, MANIFEST_SESSION_ID_LEN - 1);
    header.started_unix = started_unix;

    return write_journal(path, false, &header, sizeof(header));
}

static esp_err_t replay(manifest_t *manifest, const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    manifest_header_t header;
    if (fread(&header, sizeof(header), 1, file) != 1) {
        fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }

    if (memcmp(header.magic, MANIFEST_MAGIC, MANIFEST_MAGIC_LEN) != 0) {
        fclose(file);
        return ESP_ERR_INVALID_STATE;
    }

    memset(manifest, 0, sizeof(*manifest));
    memcpy(manifest->session_id, header.session_id, MANIFEST_SESSION_ID_LEN);
    manifest->session_id[MANIFEST_SESSION_ID_LEN - 1] = '\0';
    manifest->started_unix = header.started_unix;

    size_t valid_length = sizeof(header);
    manifest_record_t record;

    while (fread(&record, sizeof(record), 1, file) == 1) {
        apply_record(manifest, &record);
        valid_length += sizeof(record);
    }

    fseek(file, 0, SEEK_END);
    long file_length = ftell(file);
    fclose(file);

    if (file_length > (long)valid_length) {
        if (truncate(path, (off_t)valid_length) != 0) {
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

esp_err_t manifest_open(manifest_t *manifest, const char *path) {
    pthread_mutex_lock(&journal_lock);
    esp_err_t status = replay(manifest, path);
    pthread_mutex_unlock(&journal_lock);
    return status;
}

esp_err_t manifest_record_segment(manifest_t *manifest, const char *path, size_t num_bytes) {
    if (manifest->num_segments >= MANIFEST_MAX_SEGMENTS) {
        return ESP_ERR_NO_MEM;
    }

    manifest_record_t record = {
        .type = RECORD_SEGMENT_CLOSED,
        .index = (uint16_t)manifest->num_segments,
        .num_bytes = (uint32_t)num_bytes,
    };

    esp_err_t status = append_record(path, &record);
    if (status != ESP_OK) {
        return status;
    }

    apply_record(manifest, &record);
    return ESP_OK;
}

esp_err_t manifest_record_upload(manifest_t *manifest, const char *path, int index) {
    if (index < 0 || index >= manifest->num_segments) {
        return ESP_ERR_INVALID_ARG;
    }

    manifest_record_t record = {
        .type = RECORD_SEGMENT_UPLOADED,
        .index = (uint16_t)index,
        .num_bytes = 0,
    };

    esp_err_t status = append_record(path, &record);
    if (status != ESP_OK) {
        return status;
    }

    apply_record(manifest, &record);
    return ESP_OK;
}

esp_err_t manifest_record_complete(manifest_t *manifest, const char *path) {
    manifest_record_t record = {
        .type = RECORD_SESSION_COMPLETE,
        .index = 0,
        .num_bytes = 0,
    };

    esp_err_t status = append_record(path, &record);
    if (status != ESP_OK) {
        return status;
    }

    apply_record(manifest, &record);
    return ESP_OK;
}

esp_err_t manifest_record_heartbeat(const char *path) {
    manifest_record_t record = {
        .type = RECORD_HEARTBEAT,
        .index = 0,
        .num_bytes = 0,
    };

    return append_record(path, &record);
}

esp_err_t manifest_record_resume(manifest_t *manifest, const char *path) {
    manifest_record_t record = {
        .type = RECORD_SESSION_RESUMED,
        .index = 0,
        .num_bytes = 0,
    };

    esp_err_t status = append_record(path, &record);
    if (status != ESP_OK) {
        return status;
    }

    apply_record(manifest, &record);
    return ESP_OK;
}

esp_err_t manifest_record_metadata_upload(manifest_t *manifest, const char *path) {
    manifest_record_t record = {
        .type = RECORD_METADATA_UPLOADED,
        .index = 0,
        .num_bytes = 0,
    };

    esp_err_t status = append_record(path, &record);
    if (status != ESP_OK) {
        return status;
    }

    apply_record(manifest, &record);
    return ESP_OK;
}

esp_err_t manifest_record_mute(manifest_t *manifest, const char *path, int segment, uint32_t start_frame,
                               uint32_t num_frames) {
    if (segment < 0 || segment >= MANIFEST_MAX_SEGMENTS || start_frame > MANIFEST_MAX_MUTE_START
        || num_frames == 0 || num_frames > MANIFEST_MAX_MUTED_RUN) {
        return ESP_ERR_INVALID_ARG;
    }

    manifest_record_t record = {
        .type = RECORD_MUTED,
        .index = (uint16_t)segment,
        .num_bytes = start_frame | (num_frames << MANIFEST_MUTE_START_BITS),
    };

    esp_err_t status = append_record(path, &record);
    if (status != ESP_OK) {
        return status;
    }

    apply_record(manifest, &record);
    return ESP_OK;
}

int manifest_next_upload(const manifest_t *manifest) {
    for (int i = 0; i < manifest->num_segments; i++) {
        if (!manifest->segments[i].uploaded) {
            return i;
        }
    }
    return -1;
}

int manifest_num_pending(const manifest_t *manifest) {
    int num_pending = 0;
    for (int i = 0; i < manifest->num_segments; i++) {
        if (!manifest->segments[i].uploaded) {
            num_pending++;
        }
    }
    return num_pending;
}

bool manifest_fully_uploaded(const manifest_t *manifest) {
    return manifest->complete && manifest->metadata_uploaded && manifest_num_pending(manifest) == 0;
}
