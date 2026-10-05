#include <stdint.h>
#include "ring_buffer.h"
#include "tests.h"
#include "unity.h"

#define CAPACITY 8

static int16_t in[CAPACITY];
static int16_t out[CAPACITY];

static void fill(int16_t first, size_t num_samples) {
    for (size_t i = 0; i < num_samples; i++) {
        in[i] = (int16_t)(first + i);
    }
}

static void test_round_trip_preserves_order(void) {
    TEST_ASSERT_EQUAL(ESP_OK, ring_buffer_init(CAPACITY));
    fill(100, 5);

    TEST_ASSERT_EQUAL(ESP_OK, ring_buffer_write(in, 5));
    TEST_ASSERT_EQUAL_size_t(5, ring_buffer_available());
    TEST_ASSERT_EQUAL(ESP_OK, ring_buffer_read(out, 5));
    TEST_ASSERT_EQUAL_INT16_ARRAY(in, out, 5);
    TEST_ASSERT_EQUAL_size_t(0, ring_buffer_available());
}

static void test_wraps_around_the_end(void) {
    TEST_ASSERT_EQUAL(ESP_OK, ring_buffer_init(CAPACITY));
    fill(0, 6);
    TEST_ASSERT_EQUAL(ESP_OK, ring_buffer_write(in, 6));
    TEST_ASSERT_EQUAL(ESP_OK, ring_buffer_read(out, 6));

    fill(200, 7);
    TEST_ASSERT_EQUAL(ESP_OK, ring_buffer_write(in, 7));
    TEST_ASSERT_EQUAL(ESP_OK, ring_buffer_read(out, 7));
    TEST_ASSERT_EQUAL_INT16_ARRAY(in, out, 7);
}

static void test_overflow_is_rejected_whole(void) {
    TEST_ASSERT_EQUAL(ESP_OK, ring_buffer_init(CAPACITY));
    fill(0, CAPACITY);
    TEST_ASSERT_EQUAL(ESP_OK, ring_buffer_write(in, 6));

    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, ring_buffer_write(in, 3));
    TEST_ASSERT_EQUAL_size_t(6, ring_buffer_available());

    TEST_ASSERT_EQUAL(ESP_OK, ring_buffer_write(in, 2));
    TEST_ASSERT_EQUAL_size_t(CAPACITY, ring_buffer_available());
}

static void test_reading_more_than_stored_is_rejected(void) {
    TEST_ASSERT_EQUAL(ESP_OK, ring_buffer_init(CAPACITY));
    fill(0, 3);
    TEST_ASSERT_EQUAL(ESP_OK, ring_buffer_write(in, 3));

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, ring_buffer_read(out, 4));
    TEST_ASSERT_EQUAL_size_t(3, ring_buffer_available());
}

void run_ring_buffer_tests(void) {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_round_trip_preserves_order);
    RUN_TEST(test_wraps_around_the_end);
    RUN_TEST(test_overflow_is_rejected_whole);
    RUN_TEST(test_reading_more_than_stored_is_rejected);
}
