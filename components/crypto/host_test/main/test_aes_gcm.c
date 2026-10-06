#include <string.h>
#include "aes_gcm.h"
#include "kat.h"
#include "tests.h"
#include "unity.h"

#define PLAIN_BYTES   2400
#define RECORD_BYTES  (PLAIN_BYTES + AES_GCM_RECORD_OVERHEAD_BYTES)
#define NONCE_OFFSET  4
#define CIPHER_OFFSET (NONCE_OFFSET + AES_GCM_NONCE_BYTES)

static uint8_t plain[PLAIN_BYTES];
static uint8_t record[RECORD_BYTES];
static uint8_t opened[PLAIN_BYTES];

static size_t seal(void) {
    for (size_t i = 0; i < sizeof(plain); i++) {
        plain[i] = (uint8_t)(i * 31 + 11);
    }

    size_t record_bytes = 0;
    TEST_ASSERT_EQUAL(ESP_OK, aes_gcm_seal_chunk(kat_key, KAT_SEGMENT_INDEX, KAT_CHUNK_INDEX, plain, sizeof(plain),
                                                 record, sizeof(record), &record_bytes));
    TEST_ASSERT_EQUAL_size_t(RECORD_BYTES, record_bytes);
    return record_bytes;
}

static esp_err_t open_record(uint16_t segment_index, uint32_t chunk_index, size_t num_record_bytes) {
    size_t plain_bytes = 0;
    size_t consumed = 0;
    return aes_gcm_open_chunk(kat_key, segment_index, chunk_index, record, num_record_bytes,
                              opened, sizeof(opened), &plain_bytes, &consumed);
}

static void test_round_trip_is_byte_identical(void) {
    size_t record_bytes = seal();

    size_t plain_bytes = 0;
    size_t consumed = 0;
    TEST_ASSERT_EQUAL(ESP_OK, aes_gcm_open_chunk(kat_key, KAT_SEGMENT_INDEX, KAT_CHUNK_INDEX, record, record_bytes,
                                                 opened, sizeof(opened), &plain_bytes, &consumed));
    TEST_ASSERT_EQUAL_size_t(PLAIN_BYTES, plain_bytes);
    TEST_ASSERT_EQUAL_size_t(RECORD_BYTES, consumed);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(plain, opened, PLAIN_BYTES);

    TEST_ASSERT_EQUAL_UINT8(PLAIN_BYTES & 0xFF, record[0]);
    TEST_ASSERT_EQUAL_UINT8(PLAIN_BYTES >> 8, record[1]);
    TEST_ASSERT_FALSE(memcmp(record + CIPHER_OFFSET, plain, 16) == 0);
}

static void test_every_seal_draws_a_fresh_nonce(void) {
    seal();
    uint8_t first_nonce[AES_GCM_NONCE_BYTES];
    memcpy(first_nonce, record + NONCE_OFFSET, sizeof(first_nonce));

    seal();
    TEST_ASSERT_FALSE(memcmp(first_nonce, record + NONCE_OFFSET, sizeof(first_nonce)) == 0);
}

static void test_aad_binds_segment_and_chunk_index(void) {
    size_t record_bytes = seal();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_CRC, open_record(KAT_SEGMENT_INDEX + 1, KAT_CHUNK_INDEX, record_bytes));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_CRC, open_record(KAT_SEGMENT_INDEX, KAT_CHUNK_INDEX + 1, record_bytes));
}

static void test_a_flipped_bit_fails_the_tag(void) {
    size_t record_bytes = seal();

    record[CIPHER_OFFSET] ^= 0x01;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_CRC, open_record(KAT_SEGMENT_INDEX, KAT_CHUNK_INDEX, record_bytes));

    record[CIPHER_OFFSET] ^= 0x01;
    record[RECORD_BYTES - 1] ^= 0x80;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_CRC, open_record(KAT_SEGMENT_INDEX, KAT_CHUNK_INDEX, record_bytes));
}

static void test_a_torn_record_is_rejected_as_a_size_error(void) {
    size_t record_bytes = seal();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, open_record(KAT_SEGMENT_INDEX, KAT_CHUNK_INDEX, record_bytes - 1));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE,
                      open_record(KAT_SEGMENT_INDEX, KAT_CHUNK_INDEX, AES_GCM_RECORD_OVERHEAD_BYTES - 1));
}

static void test_seal_rejects_empty_input_and_a_short_buffer(void) {
    size_t record_bytes = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, aes_gcm_seal_chunk(kat_key, 0, 0, plain, 0,
                                                              record, sizeof(record), &record_bytes));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, aes_gcm_seal_chunk(kat_key, 0, 0, plain, sizeof(plain),
                                                               record, sizeof(record) - 1, &record_bytes));
}

static void test_opens_a_record_sealed_by_the_pc_tools(void) {
    size_t plain_bytes = 0;
    size_t consumed = 0;
    TEST_ASSERT_EQUAL(ESP_OK, aes_gcm_open_chunk(kat_key, KAT_SEGMENT_INDEX, KAT_CHUNK_INDEX,
                                                 kat_pc_record, sizeof(kat_pc_record),
                                                 opened, sizeof(opened), &plain_bytes, &consumed));
    TEST_ASSERT_EQUAL_size_t(KAT_PC_PLAIN_BYTES, plain_bytes);
    TEST_ASSERT_EQUAL_size_t(sizeof(kat_pc_record), consumed);

    for (size_t i = 0; i < KAT_PC_PLAIN_BYTES; i++) {
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(i * 7 + 11), opened[i]);
    }
}

void run_aes_gcm_tests(void) {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_round_trip_is_byte_identical);
    RUN_TEST(test_every_seal_draws_a_fresh_nonce);
    RUN_TEST(test_aad_binds_segment_and_chunk_index);
    RUN_TEST(test_a_flipped_bit_fails_the_tag);
    RUN_TEST(test_a_torn_record_is_rejected_as_a_size_error);
    RUN_TEST(test_seal_rejects_empty_input_and_a_short_buffer);
    RUN_TEST(test_opens_a_record_sealed_by_the_pc_tools);
}
