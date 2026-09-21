#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define MANIFEST_MAX_SEGMENTS 256
#define MANIFEST_SESSION_ID_LEN 24

typedef struct {
    size_t num_bytes;
    bool uploaded;
} manifest_segment_t;

typedef struct {
    char session_id[MANIFEST_SESSION_ID_LEN];
    int64_t started_unix;
    int num_segments;
    bool complete;
    manifest_segment_t segments[MANIFEST_MAX_SEGMENTS];
} manifest_t;

esp_err_t manifest_create(manifest_t *manifest, const char *path, const char *session_id, int64_t started_unix);
esp_err_t manifest_open(manifest_t *manifest, const char *path);
esp_err_t manifest_record_segment(manifest_t *manifest, const char *path, size_t num_bytes);
esp_err_t manifest_record_upload(manifest_t *manifest, const char *path, int index);
esp_err_t manifest_record_complete(manifest_t *manifest, const char *path);
void manifest_segment_filename(int index, char *out, size_t out_len);