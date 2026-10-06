#include <stdio.h>
#include "kat.h"
#include "sd_card.h"
#include "sha1_stream.h"
#include "unity.h"

#define PATTERN_PATH SD_CARD_MOUNT_POINT "/test_sha1.bin"

static void write_pattern(size_t num_bytes) {
    FILE *file = fopen(PATTERN_PATH, "wb");
    TEST_ASSERT_NOT_NULL(file);

    for (size_t i = 0; i < num_bytes; i++) {
        fputc((uint8_t)(i * 7 + 3), file);
    }

    TEST_ASSERT_EQUAL(0, fclose(file));
}

TEST_CASE("SHA1 of a file on the card matches Python hashlib", "[crypto][sd]") {
    TEST_ASSERT_EQUAL(ESP_OK, sd_card_mount());
    write_pattern(KAT_SHA1_PATTERN_BYTES);

    char hex[SHA1_STREAM_HEX_LEN];
    size_t hashed = 0;
    esp_err_t status = sha1_stream_file(PATTERN_PATH, hex, sizeof(hex), &hashed);
    remove(PATTERN_PATH);
    TEST_ASSERT_EQUAL(ESP_OK, sd_card_unmount());

    TEST_ASSERT_EQUAL(ESP_OK, status);
    TEST_ASSERT_EQUAL_size_t(KAT_SHA1_PATTERN_BYTES, hashed);
    TEST_ASSERT_EQUAL_STRING(KAT_SHA1_PATTERN_HEX, hex);
}
