#include "backpressure.h"
#include "tests.h"
#include "unity.h"

#define CAPACITY 160000

static void test_engages_only_above_60_pct(void) {
    TEST_ASSERT_FALSE(backpressure_update(false, 0, CAPACITY));
    TEST_ASSERT_FALSE(backpressure_update(false, 96000, CAPACITY));
    TEST_ASSERT_TRUE(backpressure_update(false, 96001, CAPACITY));
    TEST_ASSERT_TRUE(backpressure_update(false, CAPACITY, CAPACITY));
}

static void test_releases_only_below_40_pct(void) {
    TEST_ASSERT_TRUE(backpressure_update(true, CAPACITY, CAPACITY));
    TEST_ASSERT_TRUE(backpressure_update(true, 64000, CAPACITY));
    TEST_ASSERT_FALSE(backpressure_update(true, 63999, CAPACITY));
    TEST_ASSERT_FALSE(backpressure_update(true, 0, CAPACITY));
}

static void test_holds_between_thresholds(void) {
    for (size_t occupancy = 64000; occupancy <= 96000; occupancy += 8000) {
        TEST_ASSERT_FALSE(backpressure_update(false, occupancy, CAPACITY));
        TEST_ASSERT_TRUE(backpressure_update(true, occupancy, CAPACITY));
    }
}

static void test_rise_and_fall_switch_once_each(void) {
    bool engaged = false;
    int num_changes = 0;

    for (size_t occupancy = 0; occupancy <= CAPACITY; occupancy += 512) {
        bool next = backpressure_update(engaged, occupancy, CAPACITY);
        num_changes += next != engaged;
        engaged = next;
    }
    TEST_ASSERT_TRUE(engaged);

    for (size_t occupancy = CAPACITY; occupancy >= 512; occupancy -= 512) {
        bool next = backpressure_update(engaged, occupancy, CAPACITY);
        num_changes += next != engaged;
        engaged = next;
    }
    TEST_ASSERT_FALSE(engaged);
    TEST_ASSERT_EQUAL_INT(2, num_changes);
}

void run_backpressure_tests(void) {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_engages_only_above_60_pct);
    RUN_TEST(test_releases_only_below_40_pct);
    RUN_TEST(test_holds_between_thresholds);
    RUN_TEST(test_rise_and_fall_switch_once_each);
}
