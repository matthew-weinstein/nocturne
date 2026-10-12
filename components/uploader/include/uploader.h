#pragma once

#include "esp_err.h"
#include "session_json.h"

typedef void (*uploader_handler_t)(esp_err_t result);

esp_err_t uploader_init(uploader_handler_t on_finalized);
esp_err_t uploader_finalize_session(const char *session_dir, const session_json_t *session);
esp_err_t uploader_upload_session(const char *session_dir, int *out_num_uploaded);
