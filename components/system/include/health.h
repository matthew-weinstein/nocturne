#pragma once

#include <stdbool.h>
#include <stdint.h>

#define HEALTH_MIN_FREE_PCT           10
#define HEALTH_MAX_SILENT_MS          30000
#define HEALTH_MAX_UPLOAD_QUEUE_DEPTH 20

typedef enum {
    HEALTH_PROBLEM_SD_MISSING      = 1 << 0,
    HEALTH_PROBLEM_SD_FULL         = 1 << 1,
    HEALTH_PROBLEM_RECORDER_FAILED = 1 << 2,
    HEALTH_PROBLEM_BUFFER_OVERFLOW = 1 << 3,
    HEALTH_PROBLEM_MIC_SILENT      = 1 << 4,
    HEALTH_PROBLEM_UPLOAD_BACKLOG  = 1 << 5,
} health_problem_t;

typedef struct {
    bool sd_mounted;
    uint32_t free_space_pct;
    bool recorder_failed;
    uint32_t overflow_count;
    uint32_t silent_ms;
    uint32_t upload_queue_depth;
    bool wifi_reconnecting;
} health_inputs_t;

uint32_t health_evaluate(const health_inputs_t *inputs);
