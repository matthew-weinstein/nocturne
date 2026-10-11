#include "health.h"
#include "tests.h"
#include "unity.h"

static health_inputs_t healthy_inputs(void) {
    return (health_inputs_t){
        .sd_mounted = true,
        .free_space_pct = 50,
        .recorder_failed = false,
        .overflow_count = 0,
        .silent_ms = 0,
    };
}

static void test_healthy_inputs_report_no_problem(void) {
    health_inputs_t inputs = healthy_inputs();
    TEST_ASSERT_EQUAL_HEX32(0, health_evaluate(&inputs));
}

static void test_missing_card_is_not_also_full(void) {
    health_inputs_t inputs = healthy_inputs();
    inputs.sd_mounted = false;
    inputs.free_space_pct = 0;
    TEST_ASSERT_EQUAL_HEX32(HEALTH_PROBLEM_SD_MISSING, health_evaluate(&inputs));
}

static void test_free_space_must_exceed_the_minimum(void) {
    health_inputs_t inputs = healthy_inputs();

    inputs.free_space_pct = HEALTH_MIN_FREE_PCT;
    TEST_ASSERT_EQUAL_HEX32(HEALTH_PROBLEM_SD_FULL, health_evaluate(&inputs));

    inputs.free_space_pct = HEALTH_MIN_FREE_PCT + 1;
    TEST_ASSERT_EQUAL_HEX32(0, health_evaluate(&inputs));
}

static void test_failed_recorder(void) {
    health_inputs_t inputs = healthy_inputs();
    inputs.recorder_failed = true;
    TEST_ASSERT_EQUAL_HEX32(HEALTH_PROBLEM_RECORDER_FAILED, health_evaluate(&inputs));
}

static void test_a_single_overflow(void) {
    health_inputs_t inputs = healthy_inputs();
    inputs.overflow_count = 1;
    TEST_ASSERT_EQUAL_HEX32(HEALTH_PROBLEM_BUFFER_OVERFLOW, health_evaluate(&inputs));
}

static void test_mic_silent_for_the_limit(void) {
    health_inputs_t inputs = healthy_inputs();

    inputs.silent_ms = HEALTH_MAX_SILENT_MS - 1;
    TEST_ASSERT_EQUAL_HEX32(0, health_evaluate(&inputs));

    inputs.silent_ms = HEALTH_MAX_SILENT_MS;
    TEST_ASSERT_EQUAL_HEX32(HEALTH_PROBLEM_MIC_SILENT, health_evaluate(&inputs));
}

static void test_problems_combine(void) {
    health_inputs_t inputs = {
        .sd_mounted = true,
        .free_space_pct = 5,
        .recorder_failed = true,
        .overflow_count = 3,
        .silent_ms = HEALTH_MAX_SILENT_MS,
    };
    uint32_t expected = HEALTH_PROBLEM_SD_FULL | HEALTH_PROBLEM_RECORDER_FAILED |
                        HEALTH_PROBLEM_BUFFER_OVERFLOW | HEALTH_PROBLEM_MIC_SILENT;
    TEST_ASSERT_EQUAL_HEX32(expected, health_evaluate(&inputs));
}

void run_health_tests(void) {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_healthy_inputs_report_no_problem);
    RUN_TEST(test_missing_card_is_not_also_full);
    RUN_TEST(test_free_space_must_exceed_the_minimum);
    RUN_TEST(test_failed_recorder);
    RUN_TEST(test_a_single_overflow);
    RUN_TEST(test_mic_silent_for_the_limit);
    RUN_TEST(test_problems_combine);
}
