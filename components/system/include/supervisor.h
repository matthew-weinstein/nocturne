#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    SUPERVISOR_STATE_BOOT,
    SUPERVISOR_STATE_IDLE,
    SUPERVISOR_STATE_RECORDING,
    SUPERVISOR_STATE_FINALIZING,
    SUPERVISOR_STATE_RESUMING,
    SUPERVISOR_STATE_FAULT,
} supervisor_state_t;

typedef enum {
    SUPERVISOR_EVENT_BOOT_DONE,
    SUPERVISOR_EVENT_RESUME_FOUND,
    SUPERVISOR_EVENT_RESUMED,
    SUPERVISOR_EVENT_SHORT_PRESS,
    SUPERVISOR_EVENT_LONG_PRESS,
    SUPERVISOR_EVENT_VERY_LONG_PRESS,
    SUPERVISOR_EVENT_FINALIZE_OK,
    SUPERVISOR_EVENT_FINALIZE_FAILED,
    SUPERVISOR_EVENT_FAULT,
    SUPERVISOR_EVENT_COUNT,
} supervisor_event_t;

typedef void (*supervisor_handler_t)(supervisor_state_t entered, supervisor_event_t event);
typedef bool (*supervisor_health_check_t)(void);
typedef bool (*supervisor_room_check_t)(void);

esp_err_t supervisor_init(supervisor_handler_t on_transition, supervisor_health_check_t check_health,
                          supervisor_room_check_t check_room);
esp_err_t supervisor_post(supervisor_event_t event);
supervisor_state_t supervisor_state(void);
