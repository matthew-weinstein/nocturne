#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define MANIFEST_MAX_SEGMENTS 256
#define MANIFEST_SESSION_ID_LEN 24
#define MANIFEST_MUTE_START_BITS 20
#define MANIFEST_MAX_MUTE_START  ((1u << MANIFEST_MUTE_START_BITS) - 1)
#define MANIFEST_MAX_MUTED_RUN   ((1u << (32 - MANIFEST_MUTE_START_BITS)) - 1)
#define MANIFEST_MAX_MUTES       64
#define MANIFEST_MAX_RESUMES     16

typedef struct {
    size_t num_bytes;
    bool uploaded;
} manifest_segment_t;

typedef struct {
    int segment;
    uint32_t start_frame;
    uint32_t num_frames;
} manifest_mute_t;

typedef struct {
    int segment;
    int64_t started_unix;
} manifest_resume_t;

typedef struct {
    char session_id[MANIFEST_SESSION_ID_LEN];
    int64_t started_unix;
    int num_segments;
    bool complete;
    int resume_count;
    manifest_resume_t resumes[MANIFEST_MAX_RESUMES];
    int num_resumes;
    bool metadata_uploaded;
    uint32_t muted_frames;
    manifest_mute_t mutes[MANIFEST_MAX_MUTES];
    int num_mutes;
    bool mutes_truncated;
    manifest_segment_t segments[MANIFEST_MAX_SEGMENTS];
} manifest_t;

esp_err_t manifest_create(manifest_t *manifest, const char *path, const char *session_id, int64_t started_unix);
esp_err_t manifest_open(manifest_t *manifest, const char *path);
esp_err_t manifest_record_segment(manifest_t *manifest, const char *path, size_t num_bytes);
esp_err_t manifest_record_upload(manifest_t *manifest, const char *path, int index);
esp_err_t manifest_record_complete(manifest_t *manifest, const char *path);
esp_err_t manifest_record_heartbeat(const char *path);
esp_err_t manifest_record_resume(manifest_t *manifest, const char *path, int64_t started_unix);
esp_err_t manifest_record_metadata_upload(manifest_t *manifest, const char *path);
esp_err_t manifest_record_mute(manifest_t *manifest, const char *path, int segment, uint32_t start_frame,
                               uint32_t num_frames);
int manifest_next_upload(const manifest_t *manifest);
int manifest_num_pending(const manifest_t *manifest);
bool manifest_fully_uploaded(const manifest_t *manifest);
void manifest_segment_filename(int index, char *out, size_t out_len);