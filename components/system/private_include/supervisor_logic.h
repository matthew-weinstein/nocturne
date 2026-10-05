#pragma once

#include <stdbool.h>
#include "led.h"
#include "supervisor.h"

typedef enum {
    SUPERVISOR_LOGIC_ACTION_NONE,
    SUPERVISOR_LOGIC_ACTION_HEALTH_CHECK,
    SUPERVISOR_LOGIC_ACTION_FACTORY_RESET,
} supervisor_logic_action_t;

supervisor_state_t supervisor_logic_next_state(supervisor_state_t current, supervisor_event_t event);
supervisor_logic_action_t supervisor_logic_action_for(supervisor_state_t current, supervisor_event_t event);
bool supervisor_logic_led_for(supervisor_state_t entered, supervisor_event_t event, led_pattern_t *out_pattern);
