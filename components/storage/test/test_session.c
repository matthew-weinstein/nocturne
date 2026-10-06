#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include "aes_gcm.h"
#include "audio_format.h"
#include "key_manager.h"
#include "manifest.h"
#include "sd_card.h"
#include "sdkconfig.h"
#include "segment_format.h"
#include "session.h"
#include "unity.h"

#define FRAMES_PER_SEGMENT  ((CONFIG_NOCTURNE_SEGMENT_SECONDS * 1000) / AUDIO_FORMAT_FRAME_MS)
#define LAST_SEGMENT_FRAMES (FRAMES_PER_SEGMENT / 2)
#define NUM_SEGMENTS        3
#define NUM_FRAMES          (2 * FRAMES_PER_SEGMENT + LAST_SEGMENT_FRAMES)

#define PACKET_BYTES       40
#define LENGTH_FIELD_BYTES 4
#define TEST_PASSPHRASE    "nocturne-storage-test"
#define PATH_LEN           128

static const int expected_frames[NUM_SEGMENTS] = {
    FRAMES_PER_SEGMENT, FRAMES_PER_SEGMENT, LAST_SEGMENT_FRAMES
};

static uint8_t packet[PACKET_BYTES];
static uint8_t record[SEGMENT_FORMAT_MAX_CHUNK_RECORD_BYTES];
static uint8_t plaintext[SEGMENT_FORMAT_MAX_CHUNK_PLAINTEXT_BYTES];
static manifest_t manifest;

static void build_packet(int frame) {
    for (int i = 0; i < PACKET_BYTES; i++) {
        packet[i] = (uint8_t)(frame * 31 + i * 7);
    }
}

static void join_path(char *out, const char *dir, const char *name) {
    int written = snprintf(out, PATH_LEN, "%s/%s", dir, name);
    TEST_ASSERT_LESS_THAN(PATH_LEN, written);
}

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void assert_frames_match(size_t num_bytes, int *next_frame) {
    size_t offset = 0;

    while (offset < num_bytes) {
        uint16_t length = (uint16_t)(plaintext[offset] | (plaintext[offset + 1] << 8));
        offset += SEGMENT_FORMAT_FRAME_LENGTH_BYTES;
        TEST_ASSERT_EQUAL_UINT16(PACKET_BYTES, length);

        build_packet(*next_frame);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(packet, plaintext + offset, PACKET_BYTES);

        offset += length;
        (*next_frame)++;
    }
}

static int verify_segment(const char *path, uint16_t segment_index, int *next_frame) {
    FILE *file = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL(file);

    uint8_t expected_header[SEGMENT_FORMAT_HEADER_BYTES];
    uint8_t header[SEGMENT_FORMAT_HEADER_BYTES];
    segment_format_header(expected_header);
    TEST_ASSERT_EQUAL_size_t(sizeof(header), fread(header, 1, sizeof(header), file));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_header, header, sizeof(header));

    int first_frame = *next_frame;

    uint32_t chunk_index = 0;
    while (fread(record, 1, LENGTH_FIELD_BYTES, file) == LENGTH_FIELD_BYTES) {
        size_t record_bytes = read_le32(record) + AES_GCM_RECORD_OVERHEAD_BYTES;
        TEST_ASSERT_LESS_OR_EQUAL_size_t(sizeof(record), record_bytes);

        size_t body_bytes = record_bytes - LENGTH_FIELD_BYTES;
        TEST_ASSERT_EQUAL_size_t(body_bytes, fread(record + LENGTH_FIELD_BYTES, 1, body_bytes, file));

        size_t plain_bytes = 0;
        size_t consumed = 0;
        TEST_ASSERT_EQUAL(ESP_OK, aes_gcm_open_chunk(key_manager_key(), segment_index, chunk_index,
                                                     record, record_bytes, plaintext, sizeof(plaintext),
                                                     &plain_bytes, &consumed));
        assert_frames_match(plain_bytes, next_frame);
        chunk_index++;
    }

    fclose(file);
    return *next_frame - first_frame;
}

static void remove_session(const char *dir) {
    DIR *handle = opendir(dir);
    TEST_ASSERT_NOT_NULL(handle);

    struct dirent *entry;
    char path[PATH_LEN];
    while ((entry = readdir(handle)) != NULL) {
        join_path(path, dir, entry->d_name);
        remove(path);
    }

    closedir(handle);
    TEST_ASSERT_EQUAL(0, rmdir(dir));
}

TEST_CASE("a session rotates segments and every chunk opens", "[storage][sd]") {
    TEST_ASSERT_EQUAL(ESP_OK, sd_card_mount());
    TEST_ASSERT_EQUAL(ESP_OK, key_manager_init(TEST_PASSPHRASE));
    TEST_ASSERT_EQUAL(ESP_OK, session_start());

    for (int frame = 0; frame < NUM_FRAMES; frame++) {
        build_packet(frame);
        TEST_ASSERT_EQUAL(ESP_OK, session_write_packet(packet, sizeof(packet)));
    }
    TEST_ASSERT_EQUAL(ESP_OK, session_finish());

    const char *dir = session_current_dir();
    char path[PATH_LEN];
    join_path(path, dir, "manifest.bin");
    TEST_ASSERT_EQUAL(ESP_OK, manifest_open(&manifest, path));
    TEST_ASSERT_TRUE(manifest.complete);
    TEST_ASSERT_EQUAL_INT(NUM_SEGMENTS, manifest.num_segments);

    int next_frame = 0;
    for (int i = 0; i < NUM_SEGMENTS; i++) {
        char filename[32];
        manifest_segment_filename(i, filename, sizeof(filename));
        join_path(path, dir, filename);

        struct stat info;
        TEST_ASSERT_EQUAL(0, stat(path, &info));
        TEST_ASSERT_EQUAL_size_t(manifest.segments[i].num_bytes, (size_t)info.st_size);
        TEST_ASSERT_EQUAL_INT(expected_frames[i], verify_segment(path, (uint16_t)i, &next_frame));
    }

    remove_session(dir);
    TEST_ASSERT_EQUAL(ESP_OK, sd_card_unmount());
}
