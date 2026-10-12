#pragma once

#include "esp_err.h"

esp_err_t system_check_session(void);
esp_err_t system_check_upload(void);
void system_check_pending_done(esp_err_t result, int num_uploaded);
