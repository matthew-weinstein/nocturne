#include <string.h>
#include "segment_format.h"
#include "tests.h"
#include "unity.h"

static uint8_t chunk[SEGMENT_FORMAT_MAX_CHUNK_PLAINTEXT_BYTES];
static uint8_t packet[AUDIO_FORMAT_MAX_PACKET_BYTES + 1];

static void test_header_bytes(void) {
    static const uint8_t expected[SEGMENT_FORMAT_HEADER_BYTES] = { 'N', 'C', 'T', '2', 1, 50, 0, 0 };
    uint8_t header[SEGMENT_FORMAT_HEADER_BYTES];
    memset(header, 0xAA, sizeof(header));

    segment_format_header(header);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, header, sizeof(expected));
}

static void test_frame_is_little_endian_length_then_packet(void) {
    for (size_t i = 0; i < sizeof(packet); i++) {
        packet[i] = (uint8_t)(i * 13 + 5);
    }

    size_t used = 0;
    TEST_ASSERT_EQUAL(ESP_OK, segment_format_append_frame(chunk, sizeof(chunk), &used, packet, 3));
    TEST_ASSERT_EQUAL(ESP_OK, segment_format_append_frame(chunk, sizeof(chunk), &used, packet, 256));
    TEST_ASSERT_EQUAL_size_t(2 + 3 + 2 + 256, used);

    TEST_ASSERT_EQUAL_UINT8(3, chunk[0]);
    TEST_ASSERT_EQUAL_UINT8(0, chunk[1]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(packet, chunk + 2, 3);
    TEST_ASSERT_EQUAL_UINT8(0x00, chunk[5]);
    TEST_ASSERT_EQUAL_UINT8(0x01, chunk[6]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(packet, chunk + 7, 256);
}

static void test_bad_sizes_are_rejected_and_leave_the_chunk_alone(void) {
    size_t used = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, segment_format_append_frame(chunk, sizeof(chunk), &used, packet, 0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE,
                      segment_format_append_frame(chunk, sizeof(chunk), &used, packet, AUDIO_FORMAT_MAX_PACKET_BYTES + 1));
    TEST_ASSERT_EQUAL_size_t(0, used);

    used = sizeof(chunk) - 4;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, segment_format_append_frame(chunk, sizeof(chunk), &used, packet, 3));
    TEST_ASSERT_EQUAL_size_t(sizeof(chunk) - 4, used);
}

static void test_a_chunk_of_maximum_frames_fits_exactly(void) {
    size_t used = 0;
    for (int frame = 0; frame < SEGMENT_FORMAT_FRAMES_PER_CHUNK; frame++) {
        TEST_ASSERT_EQUAL(ESP_OK, segment_format_append_frame(chunk, sizeof(chunk), &used,
                                                              packet, AUDIO_FORMAT_MAX_PACKET_BYTES));
    }
    TEST_ASSERT_EQUAL_size_t(sizeof(chunk), used);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, segment_format_append_frame(chunk, sizeof(chunk), &used, packet, 1));
}

void run_segment_format_tests(void) {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_header_bytes);
    RUN_TEST(test_frame_is_little_endian_length_then_packet);
    RUN_TEST(test_bad_sizes_are_rejected_and_leave_the_chunk_alone);
    RUN_TEST(test_a_chunk_of_maximum_frames_fits_exactly);
}
