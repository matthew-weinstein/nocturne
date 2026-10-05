#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "manifest.h"
#include "tests.h"
#include "unity.h"

#define SESSION_ID   "20260101_0000"
#define STARTED_UNIX 1767225600
#define HEADER_BYTES 36
#define RECORD_BYTES 7

static char path[64];
static manifest_t manifest;

static long file_length(void) {
    FILE *file = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL(file);
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fclose(file);
    return length;
}

static void append_raw(const uint8_t *bytes, size_t num_bytes) {
    FILE *file = fopen(path, "ab");
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL_size_t(num_bytes, fwrite(bytes, 1, num_bytes, file));
    fclose(file);
}

static void write_known(void) {
    TEST_ASSERT_EQUAL(ESP_OK, manifest_create(&manifest, path, SESSION_ID, STARTED_UNIX));
    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_segment(&manifest, path, 900000));
    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_segment(&manifest, path, 899000));
    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_segment(&manifest, path, 901000));
    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_upload(&manifest, path, 0));
    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_upload(&manifest, path, 2));
}

static void assert_known(const manifest_t *replayed, bool complete) {
    TEST_ASSERT_EQUAL_STRING(SESSION_ID, replayed->session_id);
    TEST_ASSERT_EQUAL_INT64(STARTED_UNIX, replayed->started_unix);
    TEST_ASSERT_EQUAL_INT(3, replayed->num_segments);
    TEST_ASSERT_EQUAL(complete, replayed->complete);

    TEST_ASSERT_EQUAL_size_t(900000, replayed->segments[0].num_bytes);
    TEST_ASSERT_EQUAL_size_t(899000, replayed->segments[1].num_bytes);
    TEST_ASSERT_EQUAL_size_t(901000, replayed->segments[2].num_bytes);

    TEST_ASSERT_TRUE(replayed->segments[0].uploaded);
    TEST_ASSERT_FALSE(replayed->segments[1].uploaded);
    TEST_ASSERT_TRUE(replayed->segments[2].uploaded);
}

static void test_replay_rebuilds_the_written_state(void) {
    write_known();
    TEST_ASSERT_EQUAL_INT(HEADER_BYTES + 5 * RECORD_BYTES, file_length());

    manifest_t replayed;
    TEST_ASSERT_EQUAL(ESP_OK, manifest_open(&replayed, path));
    assert_known(&replayed, false);
}

static void test_torn_tail_is_truncated_and_the_next_append_lands_aligned(void) {
    write_known();
    static const uint8_t fragment[3] = { 0x01, 0x03, 0x00 };
    append_raw(fragment, sizeof(fragment));

    manifest_t replayed;
    TEST_ASSERT_EQUAL(ESP_OK, manifest_open(&replayed, path));
    assert_known(&replayed, false);
    TEST_ASSERT_EQUAL_INT(HEADER_BYTES + 5 * RECORD_BYTES, file_length());

    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_complete(&replayed, path));

    TEST_ASSERT_EQUAL(ESP_OK, manifest_open(&replayed, path));
    assert_known(&replayed, true);
}

static void test_open_rejects_missing_short_and_foreign_files(void) {
    manifest_t replayed;
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, manifest_open(&replayed, path));

    static const uint8_t short_header[10] = { 'N', 'C', 'T', '1' };
    append_raw(short_header, sizeof(short_header));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, manifest_open(&replayed, path));
    remove(path);

    uint8_t foreign[HEADER_BYTES] = { 'N', 'C', 'T', '2' };
    append_raw(foreign, sizeof(foreign));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, manifest_open(&replayed, path));
}

static void test_upload_of_an_unknown_segment_is_rejected(void) {
    write_known();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, manifest_record_upload(&manifest, path, 3));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, manifest_record_upload(&manifest, path, -1));
    TEST_ASSERT_EQUAL_INT(HEADER_BYTES + 5 * RECORD_BYTES, file_length());
}

static void test_segment_filename(void) {
    char name[32];
    manifest_segment_filename(42, name, sizeof(name));
    TEST_ASSERT_EQUAL_STRING("seg_0042.opus.enc", name);
}

static void run(void (*test)(void), const char *name, int line) {
    snprintf(path, sizeof(path), "/tmp/nocturne_manifest_%d.bin", (int)getpid());
    remove(path);
    UnityDefaultTestRun(test, name, line);
    remove(path);
}

#define RUN_MANIFEST_TEST(test) run(test, #test, __LINE__)

void run_manifest_tests(void) {
    UnitySetTestFile(__FILE__);
    RUN_MANIFEST_TEST(test_replay_rebuilds_the_written_state);
    RUN_MANIFEST_TEST(test_torn_tail_is_truncated_and_the_next_append_lands_aligned);
    RUN_MANIFEST_TEST(test_open_rejects_missing_short_and_foreign_files);
    RUN_MANIFEST_TEST(test_upload_of_an_unknown_segment_is_rejected);
    RUN_MANIFEST_TEST(test_segment_filename);
}
