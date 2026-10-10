#pragma once

#include "esp_err.h"

typedef void (*recorder_handler_t)(esp_err_t result);

esp_err_t recorder_init(recorder_handler_t on_finished);
esp_err_t recorder_start(void);
esp_err_t recorder_stop(void);
