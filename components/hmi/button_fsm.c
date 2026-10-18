#include "button_fsm.h"

_Static_assert(BUTTON_FSM_LONG_PRESS_MS % BUTTON_FSM_POLL_MS == 0 &&
               BUTTON_FSM_VERY_LONG_PRESS_MS % BUTTON_FSM_POLL_MS == 0,
               "hold thresholds are matched exactly, so they must be whole poll periods");

int button_fsm_step(button_fsm_t *fsm, bool raw_pressed, button_event_t out_events[BUTTON_FSM_MAX_EVENTS]) {
    int num_events = 0;

    if (raw_pressed == fsm->pressed) {
        fsm->unstable_ms = 0;
    } else {
        fsm->unstable_ms += BUTTON_FSM_POLL_MS;

        if (fsm->unstable_ms >= BUTTON_FSM_DEBOUNCE_MS) {
            fsm->unstable_ms = 0;
            fsm->pressed = !fsm->pressed;

            if (fsm->pressed) {
                fsm->held_ms = 0;
                out_events[num_events++] = BUTTON_EVENT_DOWN;
            } else {
                out_events[num_events++] = BUTTON_EVENT_UP;
                if (fsm->held_ms < BUTTON_FSM_LONG_PRESS_MS) {
                    out_events[num_events++] = BUTTON_EVENT_SHORT;
                }
                return num_events;
            }
        }
    }

    if (!fsm->pressed) {
        return num_events;
    }

    fsm->held_ms += BUTTON_FSM_POLL_MS;

    if (fsm->held_ms == BUTTON_FSM_LONG_PRESS_MS) {
        out_events[num_events++] = BUTTON_EVENT_LONG;
    } else if (fsm->held_ms == BUTTON_FSM_VERY_LONG_PRESS_MS) {
        out_events[num_events++] = BUTTON_EVENT_VERY_LONG;
    }
    return num_events;
}
