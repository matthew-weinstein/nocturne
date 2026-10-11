#include <stdbool.h>
#include <stdint.h>
#include "sntp_time_fallback.h"
#include "tests.h"
#include "unity.h"

#define LAST_KNOWN_UNIX 1790000000

static void test_no_stored_time_leaves_the_clock(void) {
    int64_t estimate = -1;
    TEST_ASSERT_FALSE(sntp_time_fallback_estimate(0, 5000000, 0, &estimate));
    TEST_ASSERT_EQUAL_INT64(-1, estimate);
}

static void test_stored_time_before_the_minimum_is_rejected(void) {
    int64_t estimate;
    TEST_ASSERT_FALSE(sntp_time_fallback_estimate(SNTP_TIME_FALLBACK_MIN_UNIX - 1, 0, 0, &estimate));
}

static void test_cold_boot_adds_whole_seconds_of_uptime(void) {
    int64_t estimate;
    TEST_ASSERT_TRUE(sntp_time_fallback_estimate(LAST_KNOWN_UNIX, 7999999, 7, &estimate));
    TEST_ASSERT_EQUAL_INT64(LAST_KNOWN_UNIX + 7, estimate);
}

static void test_a_clock_already_ahead_is_kept(void) {
    int64_t estimate;
    TEST_ASSERT_FALSE(sntp_time_fallback_estimate(LAST_KNOWN_UNIX, 3000000, LAST_KNOWN_UNIX + 600,
                                                  &estimate));
}

static void test_a_clock_equal_to_the_estimate_is_kept(void) {
    int64_t estimate;
    TEST_ASSERT_FALSE(sntp_time_fallback_estimate(LAST_KNOWN_UNIX, 3000000, LAST_KNOWN_UNIX + 3,
                                                  &estimate));
}

static void test_negative_uptime_is_rejected(void) {
    int64_t estimate;
    TEST_ASSERT_FALSE(sntp_time_fallback_estimate(LAST_KNOWN_UNIX, -1, 0, &estimate));
}

void run_sntp_time_fallback_tests(void) {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_no_stored_time_leaves_the_clock);
    RUN_TEST(test_stored_time_before_the_minimum_is_rejected);
    RUN_TEST(test_cold_boot_adds_whole_seconds_of_uptime);
    RUN_TEST(test_a_clock_already_ahead_is_kept);
    RUN_TEST(test_a_clock_equal_to_the_estimate_is_kept);
    RUN_TEST(test_negative_uptime_is_rejected);
}
