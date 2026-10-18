#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "session_json.h"

typedef enum {
    UPLOADER_JOB_PENDING,
    UPLOADER_JOB_FINALIZE,
    UPLOADER_JOB_RECOVER,
    UPLOADER_JOB_CLEANUP,
} uploader_job_t;

typedef void (*uploader_handler_t)(uploader_job_t job, esp_err_t result, int num_done);

esp_err_t uploader_init(uploader_handler_t on_done);
esp_err_t uploader_upload_pending(const char *session_dir);
esp_err_t uploader_finalize_session(const char *session_dir, const session_json_t *session);
esp_err_t uploader_recover_session(const char *session_dir, const session_json_t *session);
esp_err_t uploader_clean_up(void);
esp_err_t uploader_set_paused(bool paused);
int uploader_pending_segments(const char *session_dir);
