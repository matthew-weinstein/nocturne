#include <stdio.h>
#include <sys/stat.h>
#include "manifest.h"
#include "sd_card.h"
#include "unity.h"

#define MANIFEST_PATH SD_CARD_MOUNT_POINT "/test_manifest.bin"
#define SESSION_ID    "20260101_0000"
#define STARTED_UNIX  1767225600
#define SEGMENT_BYTES 900000
#define HEADER_BYTES  36
#define RECORD_BYTES  7

static manifest_t manifest;

static void remount(void) {
    TEST_ASSERT_EQUAL(ESP_OK, sd_card_unmount());
    TEST_ASSERT_EQUAL(ESP_OK, sd_card_mount());
}

static long file_length(void) {
    struct stat info;
    TEST_ASSERT_EQUAL(0, stat(MANIFEST_PATH, &info));
    return (long)info.st_size;
}

static void append_torn_record(void) {
    static const uint8_t fragment[3] = { 0x01, 0x03, 0x00 };

    FILE *file = fopen(MANIFEST_PATH, "ab");
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL_size_t(sizeof(fragment), fwrite(fragment, 1, sizeof(fragment), file));
    TEST_ASSERT_EQUAL(0, fclose(file));
}

TEST_CASE("the journal truncates a torn tail on FAT and survives remounts", "[storage][sd]") {
    TEST_ASSERT_EQUAL(ESP_OK, sd_card_mount());
    TEST_ASSERT_EQUAL(ESP_OK, manifest_create(&manifest, MANIFEST_PATH, SESSION_ID, STARTED_UNIX));
    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_segment(&manifest, MANIFEST_PATH, SEGMENT_BYTES));
    append_torn_record();
    remount();

    TEST_ASSERT_EQUAL(ESP_OK, manifest_open(&manifest, MANIFEST_PATH));
    TEST_ASSERT_EQUAL_INT(1, manifest.num_segments);
    TEST_ASSERT_EQUAL_INT(HEADER_BYTES + RECORD_BYTES, file_length());

    TEST_ASSERT_EQUAL(ESP_OK, manifest_record_complete(&manifest, MANIFEST_PATH));
    remount();

    TEST_ASSERT_EQUAL(ESP_OK, manifest_open(&manifest, MANIFEST_PATH));
    remove(MANIFEST_PATH);
    TEST_ASSERT_EQUAL(ESP_OK, sd_card_unmount());

    TEST_ASSERT_EQUAL_STRING(SESSION_ID, manifest.session_id);
    TEST_ASSERT_EQUAL_INT(1, manifest.num_segments);
    TEST_ASSERT_EQUAL_size_t(SEGMENT_BYTES, manifest.segments[0].num_bytes);
    TEST_ASSERT_TRUE(manifest.complete);
}
