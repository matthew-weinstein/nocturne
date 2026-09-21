#include "manifest.h"

#include <stdio.h>
#include <string.h>

#define MANIFEST_MAGIC "NCT1"
#define MANIFEST_MAGIC_LEN 4

#define RECORD_SEGMENT_CLOSED 1
#define RECORD_SEGMENT_UPLOADED 2
#define RECORD_SESSION_COMPLETE 3

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

void manifest_segment_filename(int index, char *out, size_t out_len) {
    snprintf(out, out_len, "seg_%04d.opusraw", index);
}

static esp_err_t append_record(const char *path, const manifest_record_t *record) {
    FILE *file = fopen(path, "ab");
    if (file == NULL) {
        return ESP_FAIL;
    }

    size_t written = fwrite(record, sizeof(*record), 1, file);
    fflush(file);
    fclose(file);

    return (written == 1) ? ESP_OK : ESP_FAIL;
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

    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return ESP_FAIL;
    }

    size_t written = fwrite(&header, sizeof(header), 1, file);
    fflush(file);
    fclose(file);

    return (written == 1) ? ESP_OK : ESP_FAIL;
}

esp_err_t manifest_open(manifest_t *manifest, const char *path) {
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

    manifest_record_t record;
    while (fread(&record, sizeof(record), 1, file) == 1) {
        apply_record(manifest, &record);
    }

    fclose(file);
    return ESP_OK;
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