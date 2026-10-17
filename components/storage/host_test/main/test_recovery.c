#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#include "manifest.h"
#include "recovery.h"
#include "segment_format.h"
#include "tests.h"
#include "unity.h"

#define STARTED_UNIX   1767225600
#define PATH_LEN       128
#define WINDOW_SECONDS (60 * 60)

static char root[64];
static manifest_t manifest;
static recovery_scan_t scan;

static void session_path(const char *name, const char *file, char *out, size_t out_len) {
    snprintf(out, out_len, "%s/%s/%s", root, name, file);
}

static void make_folder(const char *name) {
    char path[PATH_LEN];
    snprintf(path, sizeof(path), "%s/%s", root, name);
    TEST_ASSERT_EQUAL_INT(0, mkdir(path, 0755));
}

static void set_mtime(const char *name, int64_t unix_time) {
    char path[PATH_LEN];
    session_path(name, "manifest.bin", path, sizeof(path));
    struct utimbuf times = { .actime = (time_t)unix_time, .modtime = (time_t)unix_time };
    TEST_ASSERT_EQUAL_INT(0, utime(path, &times));
}

static void make_session(const char *name, int num_segments, bool complete, int64_t last_written_unix) {
    make_folder(name);

    char path[PATH_LEN];
    session_path(name, "manifest.bin", path, sizeof(path));
    TEST_ASSERT_EQUAL(ESP_OK, manifest_create(&manifest, path, name, STARTED_UNIX));
    for (int i = 0; i < num_segments; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, manifest_record_segment(&manifest, path, 900000));
    }
    if (complete) {
        TEST_ASSERT_EQUAL(ESP_OK, manifest_record_complete(&manifest, path));
    }

    set_mtime(name, last_written_unix);
}

static void write_file(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(file);
    fputs(text, file);
    fclose(file);
}

static void remove_tree(const char *path) {
    DIR *dir = opendir(path);
    if (dir == NULL) {
        remove(path);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char child[PATH_LEN + sizeof(entry->d_name)];
        snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        remove_tree(child);
    }

    closedir(dir);
    rmdir(path);
}

static void test_a_missing_sessions_folder_finds_nothing(void) {
    remove_tree(root);

    TEST_ASSERT_EQUAL(ESP_OK, recovery_scan(root, &scan));
    TEST_ASSERT_EQUAL_INT(0, scan.num_incomplete);
    TEST_ASSERT_EQUAL_INT(0, scan.num_unreadable);
}

static void test_only_sessions_without_a_complete_record_are_listed(void) {
    make_session("20260101_0000", 3, true, STARTED_UNIX + 900);
    make_session("20260102_0000", 2, false, STARTED_UNIX + 600);
    make_folder("20260103_0000");

    char path[PATH_LEN];
    snprintf(path, sizeof(path), "%s/stray.txt", root);
    write_file(path, "not a session");

    TEST_ASSERT_EQUAL(ESP_OK, recovery_scan(root, &scan));
    TEST_ASSERT_EQUAL_INT(1, scan.num_incomplete);
    TEST_ASSERT_EQUAL_INT(0, scan.num_unreadable);

    char expected_dir[PATH_LEN];
    snprintf(expected_dir, sizeof(expected_dir), "%s/20260102_0000", root);
    TEST_ASSERT_EQUAL_STRING(expected_dir, scan.sessions[0].dir);
    TEST_ASSERT_EQUAL_INT(2, scan.sessions[0].num_segments);
    TEST_ASSERT_EQUAL_INT64(STARTED_UNIX + 600, scan.sessions[0].last_written_unix);
}

static void test_a_foreign_journal_is_counted_unreadable(void) {
    make_folder("20260101_0000");

    char path[PATH_LEN];
    session_path("20260101_0000", "manifest.bin", path, sizeof(path));
    write_file(path, "this is not a journal header, but it is long enough");

    TEST_ASSERT_EQUAL(ESP_OK, recovery_scan(root, &scan));
    TEST_ASSERT_EQUAL_INT(0, scan.num_incomplete);
    TEST_ASSERT_EQUAL_INT(1, scan.num_unreadable);
}

static void test_sessions_are_listed_newest_first_by_journal_mtime(void) {
    make_session("20260101_0000", 1, false, STARTED_UNIX + 300);
    make_session("19700101_0000", 1, false, STARTED_UNIX + 900);
    make_session("20260102_0000", 1, false, STARTED_UNIX + 600);
    make_session("20260103_0000", 1, false, STARTED_UNIX + 600);

    TEST_ASSERT_EQUAL(ESP_OK, recovery_scan(root, &scan));
    TEST_ASSERT_EQUAL_INT(4, scan.num_incomplete);
    TEST_ASSERT_EQUAL_INT(4, recovery_num_listed(&scan));

    const char *expected[] = { "19700101_0000", "20260103_0000", "20260102_0000", "20260101_0000" };
    for (int i = 0; i < 4; i++) {
        char expected_dir[PATH_LEN];
        snprintf(expected_dir, sizeof(expected_dir), "%s/%s", root, expected[i]);
        TEST_ASSERT_EQUAL_STRING(expected_dir, scan.sessions[i].dir);
    }
}

static void test_beyond_the_limit_the_newest_are_kept_and_all_counted(void) {
    int num_sessions = RECOVERY_MAX_SESSIONS + 3;
    for (int i = 0; i < num_sessions; i++) {
        char name[MANIFEST_SESSION_ID_LEN];
        snprintf(name, sizeof(name), "202601%02d_0000", i + 1);
        make_session(name, 1, false, STARTED_UNIX + i * 300);
    }

    TEST_ASSERT_EQUAL(ESP_OK, recovery_scan(root, &scan));
    TEST_ASSERT_EQUAL_INT(num_sessions, scan.num_incomplete);
    TEST_ASSERT_EQUAL_INT(RECOVERY_MAX_SESSIONS, recovery_num_listed(&scan));

    for (int i = 0; i < RECOVERY_MAX_SESSIONS; i++) {
        TEST_ASSERT_EQUAL_INT64(STARTED_UNIX + (num_sessions - 1 - i) * 300, scan.sessions[i].last_written_unix);
    }
}

static void test_mtime_is_read_before_a_torn_tail_is_truncated(void) {
    make_session("20260101_0000", 2, false, STARTED_UNIX);

    char path[PATH_LEN];
    session_path("20260101_0000", "manifest.bin", path, sizeof(path));
    FILE *file = fopen(path, "ab");
    TEST_ASSERT_NOT_NULL(file);
    fputc(1, file);
    fclose(file);
    set_mtime("20260101_0000", STARTED_UNIX);

    TEST_ASSERT_EQUAL(ESP_OK, recovery_scan(root, &scan));
    TEST_ASSERT_EQUAL_INT(1, scan.num_incomplete);
    TEST_ASSERT_EQUAL_INT(2, scan.sessions[0].num_segments);
    TEST_ASSERT_EQUAL_INT64(STARTED_UNIX, scan.sessions[0].last_written_unix);
}

static void set_file_mtime(const char *path, int64_t unix_time) {
    struct utimbuf times = { .actime = (time_t)unix_time, .modtime = (time_t)unix_time };
    TEST_ASSERT_EQUAL_INT(0, utime(path, &times));
}

static void write_bytes(const char *path, size_t num_bytes) {
    FILE *file = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(file);
    for (size_t i = 0; i < num_bytes; i++) {
        fputc((int)(i & 0xff), file);
    }
    fclose(file);
}

static void scan_one(void) {
    TEST_ASSERT_EQUAL(ESP_OK, recovery_scan(root, &scan));
    TEST_ASSERT_EQUAL_INT(1, scan.num_incomplete);
}

static void replay(const char *name) {
    char path[PATH_LEN];
    session_path(name, "manifest.bin", path, sizeof(path));
    TEST_ASSERT_EQUAL(ESP_OK, manifest_open(&manifest, path));
}

static void test_a_session_written_within_the_window_is_resumed(void) {
    recovery_session_t session = { .last_written_unix = STARTED_UNIX };

    TEST_ASSERT_TRUE(recovery_should_resume(&session, STARTED_UNIX, true, WINDOW_SECONDS));
    TEST_ASSERT_TRUE(recovery_should_resume(&session, STARTED_UNIX + WINDOW_SECONDS - 1, true, WINDOW_SECONDS));
}

static void test_a_session_at_or_past_the_window_is_finished(void) {
    recovery_session_t session = { .last_written_unix = STARTED_UNIX };

    TEST_ASSERT_FALSE(recovery_should_resume(&session, STARTED_UNIX + WINDOW_SECONDS, true, WINDOW_SECONDS));
    TEST_ASSERT_FALSE(recovery_should_resume(&session, STARTED_UNIX + 86400, true, WINDOW_SECONDS));
}

static void test_unreliable_time_never_resumes(void) {
    recovery_session_t session = { .last_written_unix = STARTED_UNIX };

    TEST_ASSERT_FALSE(recovery_should_resume(&session, STARTED_UNIX + 60, false, WINDOW_SECONDS));
}

static void test_a_journal_written_in_the_future_is_finished(void) {
    recovery_session_t session = { .last_written_unix = STARTED_UNIX };

    TEST_ASSERT_FALSE(recovery_should_resume(&session, STARTED_UNIX - 1, true, WINDOW_SECONDS));
}

static void test_finish_records_the_open_segment_at_its_length(void) {
    make_session("20260101_0000", 2, false, STARTED_UNIX + 600);

    char path[PATH_LEN];
    session_path("20260101_0000", "seg_0002.opus.enc", path, sizeof(path));
    write_bytes(path, 5000);
    set_file_mtime(path, STARTED_UNIX + 777);

    scan_one();
    int64_t ended_unix = 0;
    TEST_ASSERT_EQUAL(ESP_OK, recovery_finish(&scan.sessions[0], &ended_unix));
    TEST_ASSERT_EQUAL_INT64(STARTED_UNIX + 777, ended_unix);

    replay("20260101_0000");
    TEST_ASSERT_TRUE(manifest.complete);
    TEST_ASSERT_EQUAL_INT(3, manifest.num_segments);
    TEST_ASSERT_EQUAL_size_t(5000, manifest.segments[2].num_bytes);
    TEST_ASSERT_FALSE(manifest.segments[2].uploaded);
    TEST_ASSERT_EQUAL_INT(3, manifest_num_pending(&manifest));
}

static void test_finish_skips_an_open_segment_holding_only_its_header(void) {
    make_session("20260101_0000", 2, false, STARTED_UNIX + 600);

    char path[PATH_LEN];
    session_path("20260101_0000", "seg_0002.opus.enc", path, sizeof(path));
    write_bytes(path, SEGMENT_FORMAT_HEADER_BYTES);
    set_file_mtime(path, STARTED_UNIX + 602);

    scan_one();
    int64_t ended_unix = 0;
    TEST_ASSERT_EQUAL(ESP_OK, recovery_finish(&scan.sessions[0], &ended_unix));
    TEST_ASSERT_EQUAL_INT64(STARTED_UNIX + 602, ended_unix);

    replay("20260101_0000");
    TEST_ASSERT_TRUE(manifest.complete);
    TEST_ASSERT_EQUAL_INT(2, manifest.num_segments);
}

static void test_finish_without_an_open_segment_ends_at_the_journal_mtime(void) {
    make_session("20260101_0000", 2, false, STARTED_UNIX + 600);

    scan_one();
    int64_t ended_unix = 0;
    TEST_ASSERT_EQUAL(ESP_OK, recovery_finish(&scan.sessions[0], &ended_unix));
    TEST_ASSERT_EQUAL_INT64(STARTED_UNIX + 600, ended_unix);

    replay("20260101_0000");
    TEST_ASSERT_TRUE(manifest.complete);
    TEST_ASSERT_EQUAL_INT(2, manifest.num_segments);
}

static void test_a_finished_session_is_no_longer_listed(void) {
    make_session("20260101_0000", 1, false, STARTED_UNIX);

    scan_one();
    int64_t ended_unix = 0;
    TEST_ASSERT_EQUAL(ESP_OK, recovery_finish(&scan.sessions[0], &ended_unix));

    TEST_ASSERT_EQUAL(ESP_OK, recovery_scan(root, &scan));
    TEST_ASSERT_EQUAL_INT(0, scan.num_incomplete);
}

static void test_resume_records_the_open_segment_and_leaves_the_session_open(void) {
    make_session("20260101_0000", 2, false, STARTED_UNIX + 600);

    char path[PATH_LEN];
    session_path("20260101_0000", "seg_0002.opus.enc", path, sizeof(path));
    write_bytes(path, 5000);
    set_file_mtime(path, STARTED_UNIX + 777);

    char session_dir[PATH_LEN];
    snprintf(session_dir, sizeof(session_dir), "%s/%s", root, "20260101_0000");

    replay("20260101_0000");
    int64_t written_unix = 0;
    TEST_ASSERT_EQUAL(ESP_OK, recovery_record_open_segment(&manifest, session_dir, &written_unix));
    TEST_ASSERT_EQUAL_INT64(STARTED_UNIX + 777, written_unix);
    TEST_ASSERT_EQUAL_INT(3, manifest.num_segments);

    replay("20260101_0000");
    TEST_ASSERT_FALSE(manifest.complete);
    TEST_ASSERT_EQUAL_INT(3, manifest.num_segments);
    TEST_ASSERT_EQUAL_size_t(5000, manifest.segments[2].num_bytes);
}

static void test_resume_without_an_open_segment_records_nothing(void) {
    make_session("20260101_0000", 2, false, STARTED_UNIX + 600);

    char session_dir[PATH_LEN];
    snprintf(session_dir, sizeof(session_dir), "%s/%s", root, "20260101_0000");

    replay("20260101_0000");
    int64_t written_unix = 0;
    TEST_ASSERT_EQUAL(ESP_OK, recovery_record_open_segment(&manifest, session_dir, &written_unix));
    TEST_ASSERT_EQUAL_INT64(0, written_unix);

    replay("20260101_0000");
    TEST_ASSERT_FALSE(manifest.complete);
    TEST_ASSERT_EQUAL_INT(2, manifest.num_segments);
}

static void run(void (*test)(void), const char *name, int line) {
    snprintf(root, sizeof(root), "/tmp/nocturne_recovery_%d", (int)getpid());
    remove_tree(root);
    TEST_ASSERT_EQUAL_INT(0, mkdir(root, 0755));
    UnityDefaultTestRun(test, name, line);
    remove_tree(root);
}

#define RUN_RECOVERY_TEST(test) run(test, #test, __LINE__)

void run_recovery_tests(void) {
    UnitySetTestFile(__FILE__);
    RUN_RECOVERY_TEST(test_a_missing_sessions_folder_finds_nothing);
    RUN_RECOVERY_TEST(test_only_sessions_without_a_complete_record_are_listed);
    RUN_RECOVERY_TEST(test_a_foreign_journal_is_counted_unreadable);
    RUN_RECOVERY_TEST(test_sessions_are_listed_newest_first_by_journal_mtime);
    RUN_RECOVERY_TEST(test_beyond_the_limit_the_newest_are_kept_and_all_counted);
    RUN_RECOVERY_TEST(test_mtime_is_read_before_a_torn_tail_is_truncated);
    RUN_RECOVERY_TEST(test_a_session_written_within_the_window_is_resumed);
    RUN_RECOVERY_TEST(test_a_session_at_or_past_the_window_is_finished);
    RUN_RECOVERY_TEST(test_unreliable_time_never_resumes);
    RUN_RECOVERY_TEST(test_a_journal_written_in_the_future_is_finished);
    RUN_RECOVERY_TEST(test_finish_records_the_open_segment_at_its_length);
    RUN_RECOVERY_TEST(test_finish_skips_an_open_segment_holding_only_its_header);
    RUN_RECOVERY_TEST(test_finish_without_an_open_segment_ends_at_the_journal_mtime);
    RUN_RECOVERY_TEST(test_a_finished_session_is_no_longer_listed);
    RUN_RECOVERY_TEST(test_resume_records_the_open_segment_and_leaves_the_session_open);
    RUN_RECOVERY_TEST(test_resume_without_an_open_segment_records_nothing);
}
