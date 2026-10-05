#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "button.h"

#define BUTTON_FSM_POLL_MS            20
#define BUTTON_FSM_DEBOUNCE_MS        50
#define BUTTON_FSM_LONG_PRESS_MS      2000
#define BUTTON_FSM_VERY_LONG_PRESS_MS 10000

typedef struct {
    bool pressed;
    uint32_t unstable_ms;
    uint32_t held_ms;
} button_fsm_t;

bool button_fsm_step(button_fsm_t *fsm, bool raw_pressed, button_event_t *out_event);
