#pragma once

#include "esp_err.h"

esp_err_t b2_client_init(const char *key_id, const char *application_key);
esp_err_t b2_client_authorize(void);
esp_err_t b2_client_get_upload_url(void);
