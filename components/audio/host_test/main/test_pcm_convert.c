#include <stdint.h>
#include "pcm_convert.h"
#include "tests.h"
#include "unity.h"

#define NUM_CASES 11

static void test_shift_and_saturate(void) {
    static const int32_t samples[NUM_CASES] = {
        0,
        1 << PCM_CONVERT_SHIFT,
        -(1 << PCM_CONVERT_SHIFT),
        (1 << PCM_CONVERT_SHIFT) - 1,
        -1,
        INT16_MAX * (1 << PCM_CONVERT_SHIFT),
        INT16_MIN * (1 << PCM_CONVERT_SHIFT),
        (INT16_MAX + 1) * (1 << PCM_CONVERT_SHIFT),
        (INT16_MIN - 1) * (1 << PCM_CONVERT_SHIFT),
        INT32_MAX,
        INT32_MIN,
    };
    static const int16_t expected[NUM_CASES] = {
        0, 1, -1, 0, -1, INT16_MAX, INT16_MIN, INT16_MAX, INT16_MIN, INT16_MAX, INT16_MIN,
    };

    int16_t pcm[NUM_CASES];
    pcm_convert_from_i2s(samples, pcm, NUM_CASES);
    TEST_ASSERT_EQUAL_INT16_ARRAY(expected, pcm, NUM_CASES);
}

void run_pcm_convert_tests(void) {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_shift_and_saturate);
}
