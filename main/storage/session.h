#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t session_start(void);
esp_err_t session_resume(const char *existing_session_dir);
esp_err_t session_write_packet(const uint8_t *packet, size_t num_bytes);
esp_err_t session_finish(void);