#include "led_pattern.h"
#include "tests.h"
#include "unity.h"

static void test_validity(void) {
    TEST_ASSERT_TRUE(led_pattern_valid(&(led_pattern_t){ LED_COLOR_OFF, 0, 0 }));
    TEST_ASSERT_TRUE(led_pattern_valid(&(led_pattern_t){ LED_COLOR_BLUE, 0, 0 }));
    TEST_ASSERT_TRUE(led_pattern_valid(&(led_pattern_t){ LED_COLOR_GREEN, 300, 0 }));
    TEST_ASSERT_TRUE(led_pattern_valid(&(led_pattern_t){ LED_COLOR_FAULT, 1000, 2000 }));
    TEST_ASSERT_FALSE(led_pattern_valid(&(led_pattern_t){ LED_COLOR_RED, 0, 2000 }));
    TEST_ASSERT_FALSE(led_pattern_valid(&(led_pattern_t){ LED_COLOR_COUNT, 0, 0 }));
}

static void test_flash_lights_then_goes_dark_and_stops(void) {
    led_pattern_t pattern = { LED_COLOR_GREEN, 300, 0 };

    led_pattern_step_t step = led_pattern_first(&pattern);
    TEST_ASSERT_TRUE(step.lit);
    TEST_ASSERT_EQUAL_UINT32(300, step.delay_ms);

    step = led_pattern_next(&pattern, step.lit);
    TEST_ASSERT_FALSE(step.lit);
    TEST_ASSERT_EQUAL_UINT32(0, step.delay_ms);
}

static void test_pulse_alternates_with_its_own_delays(void) {
    led_pattern_t pattern = { LED_COLOR_FAULT, 1000, 2000 };

    led_pattern_step_t step = led_pattern_first(&pattern);
    TEST_ASSERT_TRUE(step.lit);
    TEST_ASSERT_EQUAL_UINT32(1000, step.delay_ms);

    for (int cycle = 0; cycle < 3; cycle++) {
        step = led_pattern_next(&pattern, step.lit);
        TEST_ASSERT_FALSE(step.lit);
        TEST_ASSERT_EQUAL_UINT32(2000, step.delay_ms);

        step = led_pattern_next(&pattern, step.lit);
        TEST_ASSERT_TRUE(step.lit);
        TEST_ASSERT_EQUAL_UINT32(1000, step.delay_ms);
    }
}

void run_led_pattern_tests(void) {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_validity);
    RUN_TEST(test_flash_lights_then_goes_dark_and_stops);
    RUN_TEST(test_pulse_alternates_with_its_own_delays);
}
