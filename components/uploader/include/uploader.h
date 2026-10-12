#pragma once

#include "esp_err.h"

esp_err_t uploader_upload_session(const char *session_dir, int *out_num_uploaded);
