#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "led.h"

typedef struct {
    bool lit;
    uint32_t delay_ms;
} led_pattern_step_t;

bool led_pattern_valid(const led_pattern_t *pattern);
led_pattern_step_t led_pattern_first(const led_pattern_t *pattern);
led_pattern_step_t led_pattern_next(const led_pattern_t *pattern, bool lit);
