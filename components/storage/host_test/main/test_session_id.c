#include <string.h>
#include <time.h>
#include "manifest.h"
#include "session_id.h"
#include "tests.h"
#include "unity.h"

static const struct tm start = {
    .tm_year = 2026 - 1900,
    .tm_mon  = 9 - 1,
    .tm_mday = 6,
    .tm_hour = 23,
    .tm_min  = 12,
};

static void test_base_id_is_local_minute(void) {
    char id[MANIFEST_SESSION_ID_LEN];
    session_id_format(&start, 0, id, sizeof(id));
    TEST_ASSERT_EQUAL_STRING("20260906_2312", id);
    TEST_ASSERT_LESS_THAN_size_t(SESSION_ID_BASE_LEN, strlen(id));
}

static void test_collision_suffix_is_two_digits(void) {
    char id[MANIFEST_SESSION_ID_LEN];

    session_id_format(&start, 1, id, sizeof(id));
    TEST_ASSERT_EQUAL_STRING("20260906_2312_01", id);

    session_id_format(&start, 99, id, sizeof(id));
    TEST_ASSERT_EQUAL_STRING("20260906_2312_99", id);
}

void run_session_id_tests(void) {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_base_id_is_local_minute);
    RUN_TEST(test_collision_suffix_is_two_digits);
}
