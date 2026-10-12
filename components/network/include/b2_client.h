#pragma once

#include "esp_err.h"

esp_err_t b2_client_init(const char *key_id, const char *application_key);
esp_err_t b2_client_authorize(void);
esp_err_t b2_client_get_upload_url(void);

/* Uploads the file at path as file_name in the bucket. file_name must need no
   percent-encoding. Only one task may call the client. */
esp_err_t b2_client_upload_file(const char *path, const char *file_name);
