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

static void test_next_upload_follows_the_journal(void) {
    TEST_ASSERT_EQUAL(ESP_OK, manifest_create(&manifest, path, SESSION_ID, STARTED_UNIX));
    TEST_ASSERT_EQUAL_INT(-1, manifest_next_upload(&manifest));

    write_known();
    TEST_ASSERT_EQUAL_INT(1, manifest_next_upload(&manifest));

    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_upload(&manifest, path, 1));
    TEST_ASSERT_EQUAL_INT(-1, manifest_next_upload(&manifest));

    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_segment(&manifest, path, 450000));
    TEST_ASSERT_EQUAL_INT(3, manifest_next_upload(&manifest));

    manifest_t replayed;
    TEST_ASSERT_EQUAL(ESP_OK, manifest_open(&replayed, path));
    TEST_ASSERT_EQUAL_INT(3, manifest_next_upload(&replayed));
}

static void test_pending_counts_closed_segments_not_uploaded(void) {
    TEST_ASSERT_EQUAL(ESP_OK, manifest_create(&manifest, path, SESSION_ID, STARTED_UNIX));
    TEST_ASSERT_EQUAL_INT(0, manifest_num_pending(&manifest));

    write_known();
    TEST_ASSERT_EQUAL_INT(1, manifest_num_pending(&manifest));

    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_segment(&manifest, path, 450000));
    TEST_ASSERT_EQUAL_INT(2, manifest_num_pending(&manifest));

    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_upload(&manifest, path, 1));
    TEST_ASSERT_EQUAL_INT(1, manifest_num_pending(&manifest));

    manifest_t replayed;
    TEST_ASSERT_EQUAL(ESP_OK, manifest_open(&replayed, path));
    TEST_ASSERT_EQUAL_INT(1, manifest_num_pending(&replayed));
}

static void test_heartbeat_is_appended_and_leaves_the_state_unchanged(void) {
    write_known();
    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_heartbeat(path));
    TEST_ASSERT_EQUAL_INT(HEADER_BYTES + 6 * RECORD_BYTES, file_length());

    manifest_t replayed;
    TEST_ASSERT_EQUAL(ESP_OK, manifest_open(&replayed, path));
    assert_known(&replayed, false);
    TEST_ASSERT_EQUAL_INT(HEADER_BYTES + 6 * RECORD_BYTES, file_length());

    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_complete(&replayed, path));
    TEST_ASSERT_EQUAL(ESP_OK, manifest_open(&replayed, path));
    assert_known(&replayed, true);
}

static void test_resume_records_are_counted_on_replay(void) {
    write_known();
    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_resume(&manifest, path, STARTED_UNIX + 3600));
    TEST_ASSERT_EQUAL_INT(1, manifest.resume_count);
    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_segment(&manifest, path, 450000));
    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_resume(&manifest, path, STARTED_UNIX + 7200));
    TEST_ASSERT_EQUAL_INT(2, manifest.resume_count);
    TEST_ASSERT_EQUAL_INT(HEADER_BYTES + 8 * RECORD_BYTES, file_length());

    manifest_t replayed;
    TEST_ASSERT_EQUAL(ESP_OK, manifest_open(&replayed, path));
    TEST_ASSERT_EQUAL_INT(2, replayed.resume_count);
    TEST_ASSERT_EQUAL_INT(4, replayed.num_segments);
    TEST_ASSERT_FALSE(replayed.complete);
}

static void assert_resume(const manifest_resume_t *resume, int segment, int64_t started_unix) {
    TEST_ASSERT_EQUAL_INT(segment, resume->segment);
    TEST_ASSERT_EQUAL_INT64(started_unix, resume->started_unix);
}

static void test_resume_anchors_are_listed_at_the_next_segment(void) {
    write_known();
    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_resume(&manifest, path, STARTED_UNIX + 3600));
    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_segment(&manifest, path, 450000));
    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_resume(&manifest, path, STARTED_UNIX + 7200));
    TEST_ASSERT_EQUAL_INT(2, manifest.num_resumes);

    manifest_t replayed;
    TEST_ASSERT_EQUAL(ESP_OK, manifest_open(&replayed, path));
    TEST_ASSERT_EQUAL_INT(2, replayed.num_resumes);
    assert_resume(&replayed.resumes[0], 3, STARTED_UNIX + 3600);
    assert_resume(&replayed.resumes[1], 4, STARTED_UNIX + 7200);
}

static void test_a_resume_without_a_time_is_counted_but_not_listed(void) {
    write_known();
    const uint8_t old_resume[RECORD_BYTES] = { 5, 0, 0, 0, 0, 0, 0 };
    append_raw(old_resume, sizeof(old_resume));
    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_resume(&manifest, path, 0));
    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_resume(&manifest, path, (int64_t)UINT32_MAX + 1));

    manifest_t replayed;
    TEST_ASSERT_EQUAL(ESP_OK, manifest_open(&replayed, path));
    TEST_ASSERT_EQUAL_INT(3, replayed.resume_count);
    TEST_ASSERT_EQUAL_INT(0, replayed.num_resumes);
}

static void test_resumes_past_the_list_are_counted_but_not_listed(void) {
    write_known();
    for (int i = 0; i < MANIFEST_MAX_RESUMES + 2; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, manifest_record_resume(&manifest, path, STARTED_UNIX + i));
    }

    manifest_t replayed;
    TEST_ASSERT_EQUAL(ESP_OK, manifest_open(&replayed, path));
    TEST_ASSERT_EQUAL_INT(MANIFEST_MAX_RESUMES + 2, replayed.resume_count);
    TEST_ASSERT_EQUAL_INT(MANIFEST_MAX_RESUMES, replayed.num_resumes);
}

static void test_a_new_journal_has_no_resumes(void) {
    write_known();
    TEST_ASSERT_EQUAL_INT(0, manifest.resume_count);

    manifest_t replayed;
    TEST_ASSERT_EQUAL(ESP_OK, manifest_open(&replayed, path));
    TEST_ASSERT_EQUAL_INT(0, replayed.resume_count);
    TEST_ASSERT_EQUAL_INT(0, replayed.num_resumes);
}

static void test_fully_uploaded_needs_completion_every_segment_and_the_metadata(void) {
    write_known();
    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_complete(&manifest, path));
    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_metadata_upload(&manifest, path));
    TEST_ASSERT_FALSE(manifest_fully_uploaded(&manifest));

    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_upload(&manifest, path, 1));
    TEST_ASSERT_TRUE(manifest_fully_uploaded(&manifest));
    TEST_ASSERT_EQUAL_INT(HEADER_BYTES + 8 * RECORD_BYTES, file_length());

    manifest_t replayed;
    TEST_ASSERT_EQUAL(ESP_OK, manifest_open(&replayed, path));
    TEST_ASSERT_TRUE(replayed.metadata_uploaded);
    TEST_ASSERT_TRUE(manifest_fully_uploaded(&replayed));
}

static void test_a_new_journal_has_no_metadata_uploaded(void) {
    write_known();
    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_upload(&manifest, path, 1));
    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_complete(&manifest, path));

    manifest_t replayed;
    TEST_ASSERT_EQUAL(ESP_OK, manifest_open(&replayed, path));
    TEST_ASSERT_FALSE(replayed.metadata_uploaded);
    TEST_ASSERT_FALSE(manifest_fully_uploaded(&replayed));
}

static void test_muted_runs_are_summed_on_replay(void) {
    write_known();
    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_mute(&manifest, path, 1, 14000, 45));
    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_mute(&manifest, path, 2, 0, MANIFEST_MAX_MUTED_RUN));
    TEST_ASSERT_EQUAL_UINT32(45 + MANIFEST_MAX_MUTED_RUN, manifest.muted_frames);

    manifest_t replayed;
    TEST_ASSERT_EQUAL(ESP_OK, manifest_open(&replayed, path));
    assert_known(&replayed, false);
    TEST_ASSERT_EQUAL_UINT32(45 + MANIFEST_MAX_MUTED_RUN, replayed.muted_frames);
}

static void assert_mute(const manifest_mute_t *mute, int segment, uint32_t start_frame, uint32_t num_frames) {
    TEST_ASSERT_EQUAL_INT(segment, mute->segment);
    TEST_ASSERT_EQUAL_UINT32(start_frame, mute->start_frame);
    TEST_ASSERT_EQUAL_UINT32(num_frames, mute->num_frames);
}

static void test_muted_runs_are_listed_with_adjacent_runs_merged(void) {
    write_known();
    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_mute(&manifest, path, 0, 100, MANIFEST_MAX_MUTED_RUN));
    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_mute(&manifest, path, 0, 100 + MANIFEST_MAX_MUTED_RUN, 5));
    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_mute(&manifest, path, 0, 9000, 40));
    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_mute(&manifest, path, 1, 0, 20));

    manifest_t replayed;
    TEST_ASSERT_EQUAL(ESP_OK, manifest_open(&replayed, path));
    TEST_ASSERT_EQUAL_INT(3, replayed.num_mutes);
    assert_mute(&replayed.mutes[0], 0, 100, MANIFEST_MAX_MUTED_RUN + 5);
    assert_mute(&replayed.mutes[1], 0, 9000, 40);
    assert_mute(&replayed.mutes[2], 1, 0, 20);
    TEST_ASSERT_FALSE(replayed.mutes_truncated);
}

static void test_mutes_past_the_list_are_counted_but_not_listed(void) {
    write_known();
    for (int i = 0; i < MANIFEST_MAX_MUTES + 2; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, manifest_record_mute(&manifest, path, 0, (uint32_t)i * 100, 10));
    }

    manifest_t replayed;
    TEST_ASSERT_EQUAL(ESP_OK, manifest_open(&replayed, path));
    TEST_ASSERT_EQUAL_INT(MANIFEST_MAX_MUTES, replayed.num_mutes);
    TEST_ASSERT_TRUE(replayed.mutes_truncated);
    TEST_ASSERT_EQUAL_UINT32((MANIFEST_MAX_MUTES + 2) * 10, replayed.muted_frames);
}

static void test_a_new_journal_has_no_muted_frames(void) {
    write_known();

    manifest_t replayed;
    TEST_ASSERT_EQUAL(ESP_OK, manifest_open(&replayed, path));
    TEST_ASSERT_EQUAL_UINT32(0, replayed.muted_frames);
    TEST_ASSERT_EQUAL_INT(0, replayed.num_mutes);
    TEST_ASSERT_FALSE(replayed.mutes_truncated);
}

static void test_a_muted_run_that_does_not_fit_is_rejected(void) {
    write_known();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, manifest_record_mute(&manifest, path, 0, 0, 0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, manifest_record_mute(&manifest, path, 0, 0, MANIFEST_MAX_MUTED_RUN + 1));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, manifest_record_mute(&manifest, path, 0, MANIFEST_MAX_MUTE_START + 1, 1));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, manifest_record_mute(&manifest, path, -1, 0, 1));
    TEST_ASSERT_EQUAL_INT(HEADER_BYTES + 5 * RECORD_BYTES, file_length());
}

static void test_append_to_a_missing_journal_creates_nothing(void) {
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, manifest_record_heartbeat(path));
    TEST_ASSERT_EQUAL(-1, access(path, F_OK));
}

static void test_a_journal_that_cannot_be_written_fails_after_its_retries(void) {
    TEST_ASSERT_EQUAL(ESP_FAIL, manifest_create(&manifest, "/tmp/nocturne_no_such_dir/manifest.bin",
                                                SESSION_ID, STARTED_UNIX));
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
    RUN_MANIFEST_TEST(test_next_upload_follows_the_journal);
    RUN_MANIFEST_TEST(test_pending_counts_closed_segments_not_uploaded);
    RUN_MANIFEST_TEST(test_heartbeat_is_appended_and_leaves_the_state_unchanged);
    RUN_MANIFEST_TEST(test_resume_records_are_counted_on_replay);
    RUN_MANIFEST_TEST(test_resume_anchors_are_listed_at_the_next_segment);
    RUN_MANIFEST_TEST(test_a_resume_without_a_time_is_counted_but_not_listed);
    RUN_MANIFEST_TEST(test_resumes_past_the_list_are_counted_but_not_listed);
    RUN_MANIFEST_TEST(test_a_new_journal_has_no_resumes);
    RUN_MANIFEST_TEST(test_fully_uploaded_needs_completion_every_segment_and_the_metadata);
    RUN_MANIFEST_TEST(test_a_new_journal_has_no_metadata_uploaded);
    RUN_MANIFEST_TEST(test_muted_runs_are_summed_on_replay);
    RUN_MANIFEST_TEST(test_muted_runs_are_listed_with_adjacent_runs_merged);
    RUN_MANIFEST_TEST(test_mutes_past_the_list_are_counted_but_not_listed);
    RUN_MANIFEST_TEST(test_a_new_journal_has_no_muted_frames);
    RUN_MANIFEST_TEST(test_a_muted_run_that_does_not_fit_is_rejected);
    RUN_MANIFEST_TEST(test_append_to_a_missing_journal_creates_nothing);
    RUN_MANIFEST_TEST(test_a_journal_that_cannot_be_written_fails_after_its_retries);
    RUN_MANIFEST_TEST(test_segment_filename);
}
