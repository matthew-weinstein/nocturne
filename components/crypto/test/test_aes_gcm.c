#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "aes_gcm.h"
#include "kat.h"
#include "unity.h"

#define CHUNK_FRAMES       50
#define CHUNK_FRAME_BYTES  46
#define FRAME_LENGTH_BYTES 2
#define CHUNK_PLAIN_BYTES  (CHUNK_FRAMES * (FRAME_LENGTH_BYTES + CHUNK_FRAME_BYTES))
#define CHUNK_RECORD_BYTES (CHUNK_PLAIN_BYTES + AES_GCM_RECORD_OVERHEAD_BYTES)
#define HEX_LINE_BYTES     32

#define RECORD_BEGIN "---- nocturne chunk record begin ----"
#define RECORD_END   "---- nocturne chunk record end ----"

static uint8_t plain[CHUNK_PLAIN_BYTES];
static uint8_t record[CHUNK_RECORD_BYTES];
static uint8_t opened[CHUNK_PLAIN_BYTES];

static void build_chunk(void) {
    size_t offset = 0;

    for (int frame = 0; frame < CHUNK_FRAMES; frame++) {
        plain[offset++] = (uint8_t)CHUNK_FRAME_BYTES;
        plain[offset++] = (uint8_t)(CHUNK_FRAME_BYTES >> 8);

        for (int i = 0; i < CHUNK_FRAME_BYTES; i++) {
            plain[offset++] = (uint8_t)(frame * 31 + i * 7 + 11);
        }
    }
}

static void print_hex(const uint8_t *bytes, size_t num_bytes) {
    printf("%s\n", RECORD_BEGIN);

    for (size_t i = 0; i < num_bytes; i++) {
        printf("%02x", bytes[i]);

        bool line_full = (i + 1) % HEX_LINE_BYTES == 0;
        if (line_full || i + 1 == num_bytes) {
            printf("\n");
        }
    }

    printf("%s\n", RECORD_END);
}

TEST_CASE("a chunk sealed on the AES accelerator opens on the PC", "[crypto]") {
    build_chunk();

    size_t record_bytes = 0;
    TEST_ASSERT_EQUAL(ESP_OK, aes_gcm_seal_chunk(kat_key, KAT_SEGMENT_INDEX, KAT_CHUNK_INDEX,
                                                 plain, sizeof(plain), record, sizeof(record), &record_bytes));
    TEST_ASSERT_EQUAL_size_t(CHUNK_RECORD_BYTES, record_bytes);

    size_t plain_bytes = 0;
    size_t consumed = 0;
    TEST_ASSERT_EQUAL(ESP_OK, aes_gcm_open_chunk(kat_key, KAT_SEGMENT_INDEX, KAT_CHUNK_INDEX,
                                                 record, record_bytes, opened, sizeof(opened),
                                                 &plain_bytes, &consumed));
    TEST_ASSERT_EQUAL_size_t(CHUNK_PLAIN_BYTES, plain_bytes);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(plain, opened, CHUNK_PLAIN_BYTES);

    print_hex(record, record_bytes);
}

TEST_CASE("the AES accelerator opens a record sealed by the PC tools", "[crypto]") {
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
