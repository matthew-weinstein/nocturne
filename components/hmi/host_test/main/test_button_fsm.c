#include <stdbool.h>
#include <stdint.h>
#include "button_fsm.h"
#include "tests.h"
#include "unity.h"

#define MAX_EVENTS 12
#define LATEST_FIRE_MS (BUTTON_FSM_DEBOUNCE_MS + BUTTON_FSM_POLL_MS)

typedef struct {
    button_fsm_t fsm;
    uint32_t now_ms;
    button_event_t events[MAX_EVENTS];
    uint32_t event_ms[MAX_EVENTS];
    int num_events;
} recorder_t;

static void drive(recorder_t *recorder, bool raw_pressed, uint32_t duration_ms) {
    for (uint32_t elapsed = 0; elapsed < duration_ms; elapsed += BUTTON_FSM_POLL_MS) {
        recorder->now_ms += BUTTON_FSM_POLL_MS;

        button_event_t events[BUTTON_FSM_MAX_EVENTS];
        int num_events = button_fsm_step(&recorder->fsm, raw_pressed, events);
        for (int i = 0; i < num_events; i++) {
            TEST_ASSERT_LESS_THAN_INT(MAX_EVENTS, recorder->num_events);
            recorder->events[recorder->num_events] = events[i];
            recorder->event_ms[recorder->num_events] = recorder->now_ms;
            recorder->num_events++;
        }
    }
}

static void assert_events(const recorder_t *recorder, const button_event_t *expected, int num_expected) {
    TEST_ASSERT_EQUAL_INT(num_expected, recorder->num_events);
    for (int i = 0; i < num_expected; i++) {
        TEST_ASSERT_EQUAL_INT(expected[i], recorder->events[i]);
    }
}

static void test_bounce_shorter_than_debounce_is_ignored(void) {
    recorder_t recorder = {0};
    drive(&recorder, true, BUTTON_FSM_DEBOUNCE_MS - BUTTON_FSM_POLL_MS);
    drive(&recorder, false, 200);

    TEST_ASSERT_EQUAL_INT(0, recorder.num_events);
    TEST_ASSERT_FALSE(recorder.fsm.pressed);
}

static void test_down_fires_when_the_press_is_accepted(void) {
    recorder_t recorder = {0};
    drive(&recorder, true, 300);

    static const button_event_t expected[] = { BUTTON_EVENT_DOWN };
    assert_events(&recorder, expected, 1);
    TEST_ASSERT_UINT32_WITHIN(LATEST_FIRE_MS / 2, LATEST_FIRE_MS / 2, recorder.event_ms[0]);
}

static void test_short_press_fires_up_then_short_on_release(void) {
    recorder_t recorder = {0};
    drive(&recorder, true, 300);
    drive(&recorder, false, 200);

    static const button_event_t expected[] = { BUTTON_EVENT_DOWN, BUTTON_EVENT_UP, BUTTON_EVENT_SHORT };
    assert_events(&recorder, expected, 3);
    TEST_ASSERT_EQUAL_UINT32(recorder.event_ms[1], recorder.event_ms[2]);
}

static void test_release_just_before_long_threshold_is_short(void) {
    recorder_t recorder = {0};
    drive(&recorder, true, BUTTON_FSM_LONG_PRESS_MS - 100);
    drive(&recorder, false, 200);

    static const button_event_t expected[] = { BUTTON_EVENT_DOWN, BUTTON_EVENT_UP, BUTTON_EVENT_SHORT };
    assert_events(&recorder, expected, 3);
}

static void test_long_press_fires_at_threshold_while_held(void) {
    recorder_t recorder = {0};
    drive(&recorder, true, 3000);

    static const button_event_t held[] = { BUTTON_EVENT_DOWN, BUTTON_EVENT_LONG };
    assert_events(&recorder, held, 2);
    TEST_ASSERT_UINT32_WITHIN(LATEST_FIRE_MS / 2, BUTTON_FSM_LONG_PRESS_MS + LATEST_FIRE_MS / 2, recorder.event_ms[1]);

    drive(&recorder, false, 200);
    static const button_event_t released[] = { BUTTON_EVENT_DOWN, BUTTON_EVENT_LONG, BUTTON_EVENT_UP };
    assert_events(&recorder, released, 3);
}

static void test_very_long_hold_emits_long_then_very_long_and_no_short(void) {
    recorder_t recorder = {0};
    drive(&recorder, true, BUTTON_FSM_VERY_LONG_PRESS_MS + 1000);
    drive(&recorder, false, 200);

    static const button_event_t expected[] = {
        BUTTON_EVENT_DOWN, BUTTON_EVENT_LONG, BUTTON_EVENT_VERY_LONG, BUTTON_EVENT_UP,
    };
    assert_events(&recorder, expected, 4);
    TEST_ASSERT_UINT32_WITHIN(LATEST_FIRE_MS / 2, BUTTON_FSM_VERY_LONG_PRESS_MS + LATEST_FIRE_MS / 2, recorder.event_ms[2]);
}

static void test_single_poll_dropout_does_not_end_a_press(void) {
    recorder_t recorder = {0};
    drive(&recorder, true, 500);
    drive(&recorder, false, BUTTON_FSM_POLL_MS);
    drive(&recorder, true, 500);
    drive(&recorder, false, 200);

    static const button_event_t expected[] = { BUTTON_EVENT_DOWN, BUTTON_EVENT_UP, BUTTON_EVENT_SHORT };
    assert_events(&recorder, expected, 3);
}

static void test_two_presses_give_two_shorts(void) {
    recorder_t recorder = {0};
    drive(&recorder, true, 200);
    drive(&recorder, false, 200);
    drive(&recorder, true, 200);
    drive(&recorder, false, 200);

    static const button_event_t expected[] = {
        BUTTON_EVENT_DOWN, BUTTON_EVENT_UP, BUTTON_EVENT_SHORT,
        BUTTON_EVENT_DOWN, BUTTON_EVENT_UP, BUTTON_EVENT_SHORT,
    };
    assert_events(&recorder, expected, 6);
}

void run_button_fsm_tests(void) {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_bounce_shorter_than_debounce_is_ignored);
    RUN_TEST(test_down_fires_when_the_press_is_accepted);
    RUN_TEST(test_short_press_fires_up_then_short_on_release);
    RUN_TEST(test_release_just_before_long_threshold_is_short);
    RUN_TEST(test_long_press_fires_at_threshold_while_held);
    RUN_TEST(test_very_long_hold_emits_long_then_very_long_and_no_short);
    RUN_TEST(test_single_poll_dropout_does_not_end_a_press);
    RUN_TEST(test_two_presses_give_two_shorts);
}
