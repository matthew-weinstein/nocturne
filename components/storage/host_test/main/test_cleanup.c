#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#include "cleanup.h"
#include "manifest.h"
#include "tests.h"
#include "unity.h"

#define STARTED_UNIX    1767225600
#define PATH_LEN        128
#define MAX_AGE_SECONDS (7 * 24 * 60 * 60)

static char root[64];
static manifest_t manifest;
static cleanup_scan_t scan;

typedef enum {
    UPLOADED_NONE,
    UPLOADED_SEGMENTS,
    UPLOADED_ALL,
} uploaded_t;

static void session_path(const char *name, const char *file, char *out, size_t out_len) {
    snprintf(out, out_len, "%s/%s/%s", root, name, file);
}

static void write_file(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(file);
    fputs(text, file);
    fclose(file);
}

static void make_session(const char *name, bool complete, uploaded_t uploaded, int64_t last_written_unix) {
    char path[PATH_LEN];
    snprintf(path, sizeof(path), "%s/%s", root, name);
    TEST_ASSERT_EQUAL_INT(0, mkdir(path, 0755));

    for (int i = 0; i < 2; i++) {
        char filename[32];
        manifest_segment_filename(i, filename, sizeof(filename));
        session_path(name, filename, path, sizeof(path));
        write_file(path, "segment");
    }
    session_path(name, "session.json", path, sizeof(path));
    write_file(path, "{}");

    session_path(name, "manifest.bin", path, sizeof(path));
    TEST_ASSERT_EQUAL(ESP_OK, manifest_create(&manifest, path, name, STARTED_UNIX));
    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_segment(&manifest, path, 900000));
    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_segment(&manifest, path, 450000));
    if (complete) {
        TEST_ASSERT_EQUAL(ESP_OK, manifest_record_complete(&manifest, path));
    }
    if (uploaded != UPLOADED_NONE) {
        TEST_ASSERT_EQUAL(ESP_OK, manifest_record_upload(&manifest, path, 0));
        TEST_ASSERT_EQUAL(ESP_OK, manifest_record_upload(&manifest, path, 1));
    }
    if (uploaded == UPLOADED_ALL) {
        TEST_ASSERT_EQUAL(ESP_OK, manifest_record_metadata_upload(&manifest, path));
    }

    struct utimbuf times = { .actime = (time_t)last_written_unix, .modtime = (time_t)last_written_unix };
    TEST_ASSERT_EQUAL_INT(0, utime(path, &times));
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

static void assert_listed(int position, const char *name) {
    char expected_dir[PATH_LEN];
    snprintf(expected_dir, sizeof(expected_dir), "%s/%s", root, name);
    TEST_ASSERT_EQUAL_STRING(expected_dir, scan.sessions[position].dir);
}

static void test_a_missing_sessions_folder_finds_nothing(void) {
    remove_tree(root);

    TEST_ASSERT_EQUAL(ESP_OK, cleanup_scan(root, &scan));
    TEST_ASSERT_EQUAL_INT(0, scan.num_deletable);
}

static void test_only_sessions_fully_in_b2_are_listed(void) {
    make_session("20260101_0000", true, UPLOADED_ALL, STARTED_UNIX);
    make_session("20260102_0000", true, UPLOADED_SEGMENTS, STARTED_UNIX);
    make_session("20260103_0000", true, UPLOADED_NONE, STARTED_UNIX);
    make_session("20260104_0000", false, UPLOADED_ALL, STARTED_UNIX);

    char path[PATH_LEN];
    snprintf(path, sizeof(path), "%s/stray.txt", root);
    write_file(path, "not a session");

    TEST_ASSERT_EQUAL(ESP_OK, cleanup_scan(root, &scan));
    TEST_ASSERT_EQUAL_INT(1, scan.num_deletable);
    assert_listed(0, "20260101_0000");
    TEST_ASSERT_EQUAL_INT64(STARTED_UNIX, scan.sessions[0].last_written_unix);
}

static void test_sessions_are_listed_oldest_first_by_journal_mtime(void) {
    make_session("20260103_0000", true, UPLOADED_ALL, STARTED_UNIX + 300);
    make_session("19700101_0000", true, UPLOADED_ALL, STARTED_UNIX + 900);
    make_session("20260102_0000", true, UPLOADED_ALL, STARTED_UNIX + 600);
    make_session("20260101_0000", true, UPLOADED_ALL, STARTED_UNIX + 600);

    TEST_ASSERT_EQUAL(ESP_OK, cleanup_scan(root, &scan));
    TEST_ASSERT_EQUAL_INT(4, cleanup_num_listed(&scan));
    assert_listed(0, "20260103_0000");
    assert_listed(1, "20260101_0000");
    assert_listed(2, "20260102_0000");
    assert_listed(3, "19700101_0000");
}

static void test_beyond_the_limit_the_oldest_are_kept_and_all_counted(void) {
    int num_sessions = CLEANUP_MAX_SESSIONS + 3;
    for (int i = 0; i < num_sessions; i++) {
        char name[MANIFEST_SESSION_ID_LEN];
        snprintf(name, sizeof(name), "202601%02d_0000", i + 1);
        make_session(name, true, UPLOADED_ALL, STARTED_UNIX + (num_sessions - i) * 300);
    }

    TEST_ASSERT_EQUAL(ESP_OK, cleanup_scan(root, &scan));
    TEST_ASSERT_EQUAL_INT(num_sessions, scan.num_deletable);
    TEST_ASSERT_EQUAL_INT(CLEANUP_MAX_SESSIONS, cleanup_num_listed(&scan));

    for (int i = 0; i < CLEANUP_MAX_SESSIONS; i++) {
        TEST_ASSERT_EQUAL_INT64(STARTED_UNIX + (i + 1) * 300, scan.sessions[i].last_written_unix);
    }
}

static void test_a_session_is_deleted_at_the_retention_age(void) {
    cleanup_session_t session = { .last_written_unix = STARTED_UNIX };

    TEST_ASSERT_FALSE(cleanup_should_delete(&session, STARTED_UNIX + MAX_AGE_SECONDS - 1, 50, MAX_AGE_SECONDS));
    TEST_ASSERT_TRUE(cleanup_should_delete(&session, STARTED_UNIX + MAX_AGE_SECONDS, 50, MAX_AGE_SECONDS));
}

static void test_a_clock_behind_the_journal_deletes_nothing_by_age(void) {
    cleanup_session_t session = { .last_written_unix = STARTED_UNIX };

    TEST_ASSERT_FALSE(cleanup_should_delete(&session, 0, 50, MAX_AGE_SECONDS));
}

static void test_a_card_over_80_pct_full_deletes_whatever_the_age(void) {
    cleanup_session_t session = { .last_written_unix = STARTED_UNIX };

    TEST_ASSERT_FALSE(cleanup_should_delete(&session, STARTED_UNIX, CLEANUP_LOW_FREE_PCT, MAX_AGE_SECONDS));
    TEST_ASSERT_TRUE(cleanup_should_delete(&session, STARTED_UNIX, CLEANUP_LOW_FREE_PCT - 1, MAX_AGE_SECONDS));
    TEST_ASSERT_TRUE(cleanup_should_delete(&session, 0, 0, MAX_AGE_SECONDS));
}

static void test_the_card_is_full_over_95_pct(void) {
    TEST_ASSERT_FALSE(cleanup_card_full(CLEANUP_FULL_FREE_PCT));
    TEST_ASSERT_TRUE(cleanup_card_full(CLEANUP_FULL_FREE_PCT - 1));
    TEST_ASSERT_TRUE(cleanup_card_full(0));
}

static void test_delete_removes_the_folder_and_leaves_the_others(void) {
    make_session("20260101_0000", true, UPLOADED_ALL, STARTED_UNIX);
    make_session("20260102_0000", true, UPLOADED_ALL, STARTED_UNIX + 300);

    char dir[PATH_LEN];
    snprintf(dir, sizeof(dir), "%s/%s", root, "20260101_0000");
    TEST_ASSERT_EQUAL(ESP_OK, cleanup_delete_session(dir));

    struct stat info;
    TEST_ASSERT_NOT_EQUAL(0, stat(dir, &info));

    TEST_ASSERT_EQUAL(ESP_OK, cleanup_scan(root, &scan));
    TEST_ASSERT_EQUAL_INT(1, scan.num_deletable);
    assert_listed(0, "20260102_0000");
}

static void test_a_folder_that_cannot_be_emptied_keeps_its_journal(void) {
    make_session("20260101_0000", true, UPLOADED_ALL, STARTED_UNIX);

    char path[PATH_LEN];
    session_path("20260101_0000", "subfolder", path, sizeof(path));
    TEST_ASSERT_EQUAL_INT(0, mkdir(path, 0755));

    char dir[PATH_LEN];
    snprintf(dir, sizeof(dir), "%s/%s", root, "20260101_0000");
    TEST_ASSERT_NOT_EQUAL(ESP_OK, cleanup_delete_session(dir));

    TEST_ASSERT_EQUAL(ESP_OK, cleanup_scan(root, &scan));
    TEST_ASSERT_EQUAL_INT(1, scan.num_deletable);
}

static void run(void (*test)(void), const char *name, int line) {
    snprintf(root, sizeof(root), "/tmp/nocturne_cleanup_%d", (int)getpid());
    remove_tree(root);
    TEST_ASSERT_EQUAL_INT(0, mkdir(root, 0755));
    UnityDefaultTestRun(test, name, line);
    remove_tree(root);
}

#define RUN_CLEANUP_TEST(test) run(test, #test, __LINE__)

void run_cleanup_tests(void) {
    UnitySetTestFile(__FILE__);
    RUN_CLEANUP_TEST(test_a_missing_sessions_folder_finds_nothing);
    RUN_CLEANUP_TEST(test_only_sessions_fully_in_b2_are_listed);
    RUN_CLEANUP_TEST(test_sessions_are_listed_oldest_first_by_journal_mtime);
    RUN_CLEANUP_TEST(test_beyond_the_limit_the_oldest_are_kept_and_all_counted);
    RUN_CLEANUP_TEST(test_a_session_is_deleted_at_the_retention_age);
    RUN_CLEANUP_TEST(test_a_clock_behind_the_journal_deletes_nothing_by_age);
    RUN_CLEANUP_TEST(test_a_card_over_80_pct_full_deletes_whatever_the_age);
    RUN_CLEANUP_TEST(test_the_card_is_full_over_95_pct);
    RUN_CLEANUP_TEST(test_delete_removes_the_folder_and_leaves_the_others);
    RUN_CLEANUP_TEST(test_a_folder_that_cannot_be_emptied_keeps_its_journal);
}
