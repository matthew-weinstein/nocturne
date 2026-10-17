#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#include "manifest.h"
#include "recovery.h"
#include "tests.h"
#include "unity.h"

#define STARTED_UNIX 1767225600
#define PATH_LEN     128

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
}
