#include "supervisor_logic.h"

supervisor_state_t supervisor_logic_next_state(supervisor_state_t current, supervisor_event_t event) {
    if (event == SUPERVISOR_EVENT_FAULT) {
        return SUPERVISOR_STATE_FAULT;
    }

    switch (current) {
    case SUPERVISOR_STATE_BOOT:
        if (event == SUPERVISOR_EVENT_BOOT_DONE) {
            return SUPERVISOR_STATE_IDLE;
        }
        if (event == SUPERVISOR_EVENT_RESUME_FOUND) {
            return SUPERVISOR_STATE_RESUMING;
        }
        break;
    case SUPERVISOR_STATE_IDLE:
        if (event == SUPERVISOR_EVENT_SHORT_PRESS) {
            return SUPERVISOR_STATE_RECORDING;
        }
        break;
    case SUPERVISOR_STATE_RECORDING:
        if (event == SUPERVISOR_EVENT_LONG_PRESS) {
            return SUPERVISOR_STATE_FINALIZING;
        }
        break;
    case SUPERVISOR_STATE_FINALIZING:
        if (event == SUPERVISOR_EVENT_FINALIZE_OK || event == SUPERVISOR_EVENT_FINALIZE_FAILED) {
            return SUPERVISOR_STATE_IDLE;
        }
        break;
    case SUPERVISOR_STATE_RESUMING:
        if (event == SUPERVISOR_EVENT_RESUMED) {
            return SUPERVISOR_STATE_RECORDING;
        }
        break;
    case SUPERVISOR_STATE_FAULT:
        break;
    }
    return current;
}

supervisor_logic_action_t supervisor_logic_action_for(supervisor_state_t current, supervisor_event_t event) {
    if (current == SUPERVISOR_STATE_RECORDING && event == SUPERVISOR_EVENT_SHORT_PRESS) {
        return SUPERVISOR_LOGIC_ACTION_HEALTH_CHECK;
    }
    if (current == SUPERVISOR_STATE_IDLE && event == SUPERVISOR_EVENT_VERY_LONG_PRESS) {
        return SUPERVISOR_LOGIC_ACTION_FACTORY_RESET;
    }
    return SUPERVISOR_LOGIC_ACTION_NONE;
}

bool supervisor_logic_led_for(supervisor_state_t entered, supervisor_event_t event, led_pattern_t *out_pattern) {
    switch (entered) {
    case SUPERVISOR_STATE_IDLE:
        if (event == SUPERVISOR_EVENT_FINALIZE_OK) {
            *out_pattern = (led_pattern_t){ LED_COLOR_WHITE, LED_RESULT_MS, 0 };
        } else if (event == SUPERVISOR_EVENT_FINALIZE_FAILED) {
            *out_pattern = (led_pattern_t){ LED_COLOR_RED, LED_RESULT_MS, 0 };
        } else {
            *out_pattern = (led_pattern_t){ LED_COLOR_OFF, 0, 0 };
        }
        return true;
    case SUPERVISOR_STATE_RECORDING:
        if (event == SUPERVISOR_EVENT_RESUMED) {
            *out_pattern = (led_pattern_t){ LED_COLOR_CYAN, LED_FLASH_MS, 0 };
        } else {
            *out_pattern = (led_pattern_t){ LED_COLOR_GREEN, LED_FLASH_MS, 0 };
        }
        return true;
    case SUPERVISOR_STATE_FINALIZING:
        *out_pattern = (led_pattern_t){ LED_COLOR_BLUE, 0, 0 };
        return true;
    case SUPERVISOR_STATE_FAULT:
        *out_pattern = (led_pattern_t){ LED_COLOR_FAULT, LED_FAULT_ON_MS, LED_FAULT_OFF_MS };
        return true;
    case SUPERVISOR_STATE_BOOT:
    case SUPERVISOR_STATE_RESUMING:
        break;
    }
    return false;
}
