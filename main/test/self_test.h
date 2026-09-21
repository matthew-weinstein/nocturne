#pragma once

#include "esp_err.h"

esp_err_t self_test_manifest(void);
esp_err_t self_test_latest_session(void);
esp_err_t self_test_run_all(void);