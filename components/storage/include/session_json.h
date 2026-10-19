#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "manifest.h"

#define SESSION_JSON_MAX_BYTES 6144

typedef struct {
    const char *session_id;
    const char *device_id;
    const char *firmware_version;
    const char *opus_application;
    int64_t started_unix;
    int64_t ended_unix;
    int segment_count;
    uint32_t bitrate;
    bool time_reliable;
    int resume_count;
    const manifest_resume_t *resumes;
    int num_resumes;
    bool recovered;
    uint32_t muted_frames;
    const manifest_mute_t *mutes;
    int num_mutes;
    bool mutes_truncated;
    uint32_t buffer_overflows;
    uint32_t sd_write_errors;
    uint32_t upload_retries;
    uint32_t wifi_disconnects;
} session_json_t;

esp_err_t session_json_format(const session_json_t *session, char *out, size_t max_bytes, size_t *out_num_bytes);
