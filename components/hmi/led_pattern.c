#include "led_pattern.h"

bool led_pattern_valid(const led_pattern_t *pattern) {
    if (pattern->color >= LED_COLOR_COUNT) {
        return false;
    }

    return pattern->on_ms > 0 || pattern->off_ms == 0;
}

led_pattern_step_t led_pattern_first(const led_pattern_t *pattern) {
    return (led_pattern_step_t){ .lit = true, .delay_ms = pattern->on_ms };
}

led_pattern_step_t led_pattern_next(const led_pattern_t *pattern, bool lit) {
    if (pattern->off_ms == 0) {
        return (led_pattern_step_t){ .lit = false, .delay_ms = 0 };
    }

    bool next_lit = !lit;
    return (led_pattern_step_t){ .lit = next_lit, .delay_ms = next_lit ? pattern->on_ms : pattern->off_ms };
}
