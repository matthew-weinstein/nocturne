#include <stdio.h>
#include <unistd.h>
#include "sha1_stream.h"
#include "tests.h"
#include "unity.h"

#define PATTERN_BYTES 10000
#define MISSING_PATH  "/tmp/nocturne_missing.bin"

static const char pattern_hex[] = "504bab9f255da75e2c3c08dfbc11061a05996bbf";
static const char empty_hex[] = "da39a3ee5e6b4b0d3255bfef95601890afd80709";

static char path[64];

static void write_pattern(size_t num_bytes) {
    snprintf(path, sizeof(path), "/tmp/nocturne_sha1_%d.bin", (int)getpid());
    FILE *file = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(file);
    for (size_t i = 0; i < num_bytes; i++) {
        fputc((uint8_t)(i * 7 + 3), file);
    }
    fclose(file);
}

static void assert_digest(size_t num_bytes, const char *expected_hex) {
    write_pattern(num_bytes);

    char hex[SHA1_STREAM_HEX_LEN];
    size_t hashed = 0;
    esp_err_t status = sha1_stream_file(path, hex, sizeof(hex), &hashed);
    remove(path);

    TEST_ASSERT_EQUAL(ESP_OK, status);
    TEST_ASSERT_EQUAL_size_t(num_bytes, hashed);
    TEST_ASSERT_EQUAL_STRING(expected_hex, hex);
}

static void test_pattern_spanning_several_reads_matches_hashlib(void) {
    assert_digest(PATTERN_BYTES, pattern_hex);
}

static void test_empty_file_matches_hashlib(void) {
    assert_digest(0, empty_hex);
}

static void test_missing_file_and_short_buffer_are_rejected(void) {
    char hex[SHA1_STREAM_HEX_LEN];
    size_t hashed = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, sha1_stream_file(MISSING_PATH, hex, sizeof(hex), &hashed));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sha1_stream_file(MISSING_PATH, hex, sizeof(hex) - 1, &hashed));
}

void run_sha1_stream_tests(void) {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_pattern_spanning_several_reads_matches_hashlib);
    RUN_TEST(test_empty_file_matches_hashlib);
    RUN_TEST(test_missing_file_and_short_buffer_are_rejected);
}
