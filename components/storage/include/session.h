#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef void (*session_segment_handler_t)(const char *session_dir);

esp_err_t session_start(session_segment_handler_t on_segment_closed);
esp_err_t session_resume(const char *existing_session_dir, session_segment_handler_t on_segment_closed);
esp_err_t session_write_packet(const uint8_t *packet, size_t num_bytes);
esp_err_t session_finish(void);
const char *session_current_dir(void);