#include "health.h"

uint32_t health_evaluate(const health_inputs_t *inputs) {
    uint32_t problems = 0;

    if (!inputs->sd_mounted) {
        problems |= HEALTH_PROBLEM_SD_MISSING;
    } else if (inputs->free_space_pct <= HEALTH_MIN_FREE_PCT) {
        problems |= HEALTH_PROBLEM_SD_FULL;
    }
    if (inputs->recorder_failed) {
        problems |= HEALTH_PROBLEM_RECORDER_FAILED;
    }
    if (inputs->overflow_count > 0) {
        problems |= HEALTH_PROBLEM_BUFFER_OVERFLOW;
    }
    if (inputs->silent_ms >= HEALTH_MAX_SILENT_MS) {
        problems |= HEALTH_PROBLEM_MIC_SILENT;
    }
    return problems;
}
