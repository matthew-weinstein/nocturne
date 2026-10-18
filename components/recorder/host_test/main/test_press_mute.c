#include <stdbool.h>
#include <stdint.h>
#include "press_mute.h"
#include "tests.h"
#include "unity.h"

#define SAMPLES_PER_MS 16
#define BEFORE         (PRESS_MUTE_BEFORE_MS * SAMPLES_PER_MS)
#define AFTER          (PRESS_MUTE_AFTER_MS * SAMPLES_PER_MS)

static bool muted_at(press_mute_t *mute, uint32_t sample) {
    int16_t value = 1;
    return press_mute_apply(mute, sample, &value, 1) == 1 && value == 0;
}

static void test_nothing_is_muted_without_a_press(void) {
    press_mute_t mute;
    press_mute_reset(&mute);

    int16_t samples[320];
    for (int i = 0; i < 320; i++) {
        samples[i] = 100;
    }
    TEST_ASSERT_EQUAL_size_t(0, press_mute_apply(&mute, 0, samples, 320));
    TEST_ASSERT_EQUAL_INT16(100, samples[319]);
}

static void test_a_press_mutes_from_before_it_until_after_the_release(void) {
    press_mute_t mute;
    press_mute_reset(&mute);
    press_mute_press(&mute, 100000);
    press_mute_release(&mute, 102000);

    TEST_ASSERT_FALSE(muted_at(&mute, 100000 - BEFORE - 1));
    TEST_ASSERT_TRUE(muted_at(&mute, 100000 - BEFORE));
    TEST_ASSERT_TRUE(muted_at(&mute, 102000 + AFTER - 1));
    TEST_ASSERT_FALSE(muted_at(&mute, 102000 + AFTER));
}

static void test_a_press_at_the_start_mutes_from_the_first_sample(void) {
    press_mute_t mute;
    press_mute_reset(&mute);
    press_mute_press(&mute, BEFORE / 2);
    press_mute_release(&mute, BEFORE);

    TEST_ASSERT_TRUE(muted_at(&mute, 0));
}

static void test_a_held_press_stays_muted_until_released(void) {
    press_mute_t mute;
    press_mute_reset(&mute);
    press_mute_press(&mute, 50000);

    TEST_ASSERT_TRUE(muted_at(&mute, 50000));
    TEST_ASSERT_TRUE(muted_at(&mute, 50000 + 10 * 16000));
    TEST_ASSERT_EQUAL_INT(1, mute.num_windows);
}

static void test_a_press_inside_another_window_extends_it(void) {
    press_mute_t mute;
    press_mute_reset(&mute);
    press_mute_press(&mute, 100000);
    press_mute_release(&mute, 101000);
    press_mute_press(&mute, 101000 + AFTER);
    press_mute_release(&mute, 110000);

    TEST_ASSERT_EQUAL_INT(1, mute.num_windows);
    TEST_ASSERT_TRUE(muted_at(&mute, 105000));
    TEST_ASSERT_TRUE(muted_at(&mute, 110000 + AFTER - 1));
}

static void test_separate_presses_leave_the_gap_between_them(void) {
    press_mute_t mute;
    press_mute_reset(&mute);
    press_mute_press(&mute, 100000);
    press_mute_release(&mute, 101000);
    press_mute_press(&mute, 200000);
    press_mute_release(&mute, 201000);

    TEST_ASSERT_EQUAL_INT(2, mute.num_windows);
    TEST_ASSERT_TRUE(muted_at(&mute, 101000));
    TEST_ASSERT_FALSE(muted_at(&mute, 150000));
    TEST_ASSERT_TRUE(muted_at(&mute, 200000));
}

static void test_a_window_is_dropped_once_audio_has_passed_it(void) {
    press_mute_t mute;
    press_mute_reset(&mute);
    press_mute_press(&mute, 100000);
    press_mute_release(&mute, 101000);

    TEST_ASSERT_FALSE(muted_at(&mute, 101000 + AFTER));
    TEST_ASSERT_EQUAL_INT(0, mute.num_windows);
}

static void test_a_release_without_a_press_does_nothing(void) {
    press_mute_t mute;
    press_mute_reset(&mute);
    press_mute_release(&mute, 100000);

    TEST_ASSERT_EQUAL_INT(0, mute.num_windows);
    TEST_ASSERT_FALSE(muted_at(&mute, 100000));
}

static void test_more_presses_than_windows_merge_into_the_last(void) {
    press_mute_t mute;
    press_mute_reset(&mute);
    for (int i = 0; i < PRESS_MUTE_MAX_WINDOWS + 1; i++) {
        uint32_t sample = 100000 + (uint32_t)i * 100000;
        press_mute_press(&mute, sample);
        press_mute_release(&mute, sample + 1000);
    }

    TEST_ASSERT_EQUAL_INT(PRESS_MUTE_MAX_WINDOWS, mute.num_windows);
    TEST_ASSERT_TRUE(muted_at(&mute, 100000 + PRESS_MUTE_MAX_WINDOWS * 100000 + 1000));
}

void run_press_mute_tests(void) {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_nothing_is_muted_without_a_press);
    RUN_TEST(test_a_press_mutes_from_before_it_until_after_the_release);
    RUN_TEST(test_a_press_at_the_start_mutes_from_the_first_sample);
    RUN_TEST(test_a_held_press_stays_muted_until_released);
    RUN_TEST(test_a_press_inside_another_window_extends_it);
    RUN_TEST(test_separate_presses_leave_the_gap_between_them);
    RUN_TEST(test_a_window_is_dropped_once_audio_has_passed_it);
    RUN_TEST(test_a_release_without_a_press_does_nothing);
    RUN_TEST(test_more_presses_than_windows_merge_into_the_last);
}
