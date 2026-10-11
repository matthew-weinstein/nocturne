#include <stdint.h>
#include "tests.h"
#include "unity.h"
#include "wifi_backoff.h"

static void test_first_retry_waits_the_minimum(void) {
    TEST_ASSERT_EQUAL_UINT32(WIFI_BACKOFF_FIRST_MS, wifi_backoff_delay_ms(0));
}

static void test_each_retry_doubles_the_wait(void) {
    TEST_ASSERT_EQUAL_UINT32(2000, wifi_backoff_delay_ms(1));
    TEST_ASSERT_EQUAL_UINT32(4000, wifi_backoff_delay_ms(2));
    TEST_ASSERT_EQUAL_UINT32(32000, wifi_backoff_delay_ms(5));
}

static void test_wait_is_capped_at_the_maximum(void) {
    TEST_ASSERT_EQUAL_UINT32(WIFI_BACKOFF_MAX_MS, wifi_backoff_delay_ms(6));
    TEST_ASSERT_EQUAL_UINT32(WIFI_BACKOFF_MAX_MS, wifi_backoff_delay_ms(7));
}

static void test_a_night_of_retries_stays_at_the_maximum(void) {
    TEST_ASSERT_EQUAL_UINT32(WIFI_BACKOFF_MAX_MS, wifi_backoff_delay_ms(1000));
    TEST_ASSERT_EQUAL_UINT32(WIFI_BACKOFF_MAX_MS, wifi_backoff_delay_ms(UINT32_MAX));
}

void run_wifi_backoff_tests(void) {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_first_retry_waits_the_minimum);
    RUN_TEST(test_each_retry_doubles_the_wait);
    RUN_TEST(test_wait_is_capped_at_the_maximum);
    RUN_TEST(test_a_night_of_retries_stays_at_the_maximum);
}
