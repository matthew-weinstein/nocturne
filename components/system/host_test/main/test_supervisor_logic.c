#include <stddef.h>
#include <stdio.h>
#include "supervisor_logic.h"
#include "tests.h"
#include "unity.h"

#define NUM_STATES (SUPERVISOR_STATE_FAULT + 1)

typedef struct {
    supervisor_state_t from;
    supervisor_event_t event;
    supervisor_state_t to;
} transition_t;

static const transition_t transitions[] = {
    { SUPERVISOR_STATE_BOOT,       SUPERVISOR_EVENT_BOOT_DONE,       SUPERVISOR_STATE_IDLE },
    { SUPERVISOR_STATE_BOOT,       SUPERVISOR_EVENT_RESUME_FOUND,    SUPERVISOR_STATE_RESUMING },
    { SUPERVISOR_STATE_IDLE,       SUPERVISOR_EVENT_SHORT_PRESS,     SUPERVISOR_STATE_RECORDING },
    { SUPERVISOR_STATE_RECORDING,  SUPERVISOR_EVENT_LONG_PRESS,      SUPERVISOR_STATE_FINALIZING },
    { SUPERVISOR_STATE_FINALIZING, SUPERVISOR_EVENT_FINALIZE_OK,     SUPERVISOR_STATE_IDLE },
    { SUPERVISOR_STATE_FINALIZING, SUPERVISOR_EVENT_FINALIZE_FAILED, SUPERVISOR_STATE_IDLE },
    { SUPERVISOR_STATE_RESUMING,   SUPERVISOR_EVENT_RESUMED,         SUPERVISOR_STATE_RECORDING },
};

static supervisor_state_t expected_next(supervisor_state_t from, supervisor_event_t event) {
    if (event == SUPERVISOR_EVENT_FAULT) {
        return SUPERVISOR_STATE_FAULT;
    }
    for (size_t i = 0; i < sizeof(transitions) / sizeof(transitions[0]); i++) {
        if (transitions[i].from == from && transitions[i].event == event) {
            return transitions[i].to;
        }
    }
    return from;
}

static void test_every_state_event_pair_matches_the_table(void) {
    for (int state = 0; state < NUM_STATES; state++) {
        for (int event = 0; event < SUPERVISOR_EVENT_COUNT; event++) {
            char message[48];
            snprintf(message, sizeof(message), "state %d, event %d", state, event);
            TEST_ASSERT_EQUAL_INT_MESSAGE(expected_next(state, event),
                                          supervisor_logic_next_state(state, event), message);
        }
    }
}

static void test_in_place_actions(void) {
    for (int state = 0; state < NUM_STATES; state++) {
        for (int event = 0; event < SUPERVISOR_EVENT_COUNT; event++) {
            supervisor_logic_action_t expected = SUPERVISOR_LOGIC_ACTION_NONE;

            if (state == SUPERVISOR_STATE_RECORDING && event == SUPERVISOR_EVENT_SHORT_PRESS) {
                expected = SUPERVISOR_LOGIC_ACTION_HEALTH_CHECK;
            } else if (state == SUPERVISOR_STATE_IDLE && event == SUPERVISOR_EVENT_VERY_LONG_PRESS) {
                expected = SUPERVISOR_LOGIC_ACTION_FACTORY_RESET;
            }

            TEST_ASSERT_EQUAL_INT(expected, supervisor_logic_action_for(state, event));
        }
    }
}

static void assert_led(supervisor_state_t entered, supervisor_event_t event,
                       led_color_t color, uint32_t on_ms, uint32_t off_ms) {
    led_pattern_t pattern;
    TEST_ASSERT_TRUE(supervisor_logic_led_for(entered, event, &pattern));
    TEST_ASSERT_EQUAL_INT(color, pattern.color);
    TEST_ASSERT_EQUAL_UINT32(on_ms, pattern.on_ms);
    TEST_ASSERT_EQUAL_UINT32(off_ms, pattern.off_ms);
}

static void test_led_for_each_state_entered(void) {
    assert_led(SUPERVISOR_STATE_IDLE, SUPERVISOR_EVENT_BOOT_DONE, LED_COLOR_OFF, 0, 0);
    assert_led(SUPERVISOR_STATE_IDLE, SUPERVISOR_EVENT_FINALIZE_OK, LED_COLOR_WHITE, LED_RESULT_MS, 0);
    assert_led(SUPERVISOR_STATE_IDLE, SUPERVISOR_EVENT_FINALIZE_FAILED, LED_COLOR_RED, LED_RESULT_MS, 0);
    assert_led(SUPERVISOR_STATE_RECORDING, SUPERVISOR_EVENT_SHORT_PRESS, LED_COLOR_GREEN, LED_FLASH_MS, 0);
    assert_led(SUPERVISOR_STATE_RECORDING, SUPERVISOR_EVENT_RESUMED, LED_COLOR_CYAN, LED_FLASH_MS, 0);
    assert_led(SUPERVISOR_STATE_FINALIZING, SUPERVISOR_EVENT_LONG_PRESS, LED_COLOR_BLUE, 0, 0);
    assert_led(SUPERVISOR_STATE_FAULT, SUPERVISOR_EVENT_FAULT, LED_COLOR_FAULT, LED_FAULT_ON_MS, LED_FAULT_OFF_MS);
}

static void test_boot_and_resuming_leave_the_led_alone(void) {
    led_pattern_t pattern;
    TEST_ASSERT_FALSE(supervisor_logic_led_for(SUPERVISOR_STATE_BOOT, SUPERVISOR_EVENT_BOOT_DONE, &pattern));
    TEST_ASSERT_FALSE(supervisor_logic_led_for(SUPERVISOR_STATE_RESUMING, SUPERVISOR_EVENT_RESUME_FOUND, &pattern));
}

void run_supervisor_logic_tests(void) {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_every_state_event_pair_matches_the_table);
    RUN_TEST(test_in_place_actions);
    RUN_TEST(test_led_for_each_state_entered);
    RUN_TEST(test_boot_and_resuming_leave_the_led_alone);
}
