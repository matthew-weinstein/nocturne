#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef void (*recorder_handler_t)(esp_err_t result);

typedef struct {
    bool failed;
    uint32_t overflow_count;
    uint32_t silent_ms;
} recorder_stats_t;

esp_err_t recorder_init(recorder_handler_t on_finished);
esp_err_t recorder_start(void);
esp_err_t recorder_stop(void);
void recorder_get_stats(recorder_stats_t *out_stats);
