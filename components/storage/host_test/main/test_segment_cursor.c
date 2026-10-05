#include "segment_cursor.h"
#include "segment_format.h"
#include "tests.h"
#include "unity.h"

#define FRAMES_PER_SEGMENT 500

static void test_flushes_every_chunk_and_closes_at_segment_length(void) {
    segment_cursor_t cursor;
    segment_cursor_reset(&cursor, FRAMES_PER_SEGMENT);

    int flushes = 0;
    for (int frame = 1; frame < FRAMES_PER_SEGMENT; frame++) {
        segment_cursor_action_t action = segment_cursor_add_frame(&cursor);

        if (frame % SEGMENT_FORMAT_FRAMES_PER_CHUNK == 0) {
            TEST_ASSERT_EQUAL_INT(SEGMENT_CURSOR_FLUSH_CHUNK, action);
            flushes++;
        } else {
            TEST_ASSERT_EQUAL_INT(SEGMENT_CURSOR_CONTINUE, action);
        }
    }

    TEST_ASSERT_EQUAL_INT(FRAMES_PER_SEGMENT / SEGMENT_FORMAT_FRAMES_PER_CHUNK - 1, flushes);
    TEST_ASSERT_EQUAL_INT(SEGMENT_CURSOR_CLOSE_SEGMENT, segment_cursor_add_frame(&cursor));
}

static void test_a_35_second_capture_splits_500_500_500_250(void) {
    static const int expected[] = { 500, 500, 500, 250 };
    int segment_frames[4] = {0};
    int segment = 0;

    segment_cursor_t cursor;
    segment_cursor_reset(&cursor, FRAMES_PER_SEGMENT);

    for (int frame = 0; frame < 1750; frame++) {
        segment_frames[segment]++;
        if (segment_cursor_add_frame(&cursor) == SEGMENT_CURSOR_CLOSE_SEGMENT) {
            segment++;
            segment_cursor_reset(&cursor, FRAMES_PER_SEGMENT);
        }
    }

    TEST_ASSERT_EQUAL_INT(3, segment);
    TEST_ASSERT_EQUAL_INT_ARRAY(expected, segment_frames, 4);
}

void run_segment_cursor_tests(void) {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_flushes_every_chunk_and_closes_at_segment_length);
    RUN_TEST(test_a_35_second_capture_splits_500_500_500_250);
}
