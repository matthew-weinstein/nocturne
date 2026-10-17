#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "manifest.h"

#define RECOVERY_MAX_SESSIONS 8
#define RECOVERY_DIR_LEN      64

typedef struct {
    char dir[RECOVERY_DIR_LEN];
    int64_t last_written_unix;
    int num_segments;
} recovery_session_t;

typedef struct {
    int num_incomplete;
    int num_unreadable;
    recovery_session_t sessions[RECOVERY_MAX_SESSIONS];
} recovery_scan_t;

esp_err_t recovery_scan(const char *sessions_dir, recovery_scan_t *scan);
int recovery_num_listed(const recovery_scan_t *scan);
bool recovery_should_resume(const recovery_session_t *session, int64_t now_unix, bool time_reliable,
                            int64_t window_seconds);
esp_err_t recovery_record_open_segment(manifest_t *manifest, const char *session_dir, int64_t *out_written_unix);
esp_err_t recovery_finish(const recovery_session_t *session, int64_t *out_ended_unix);
