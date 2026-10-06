#include <stdio.h>
#include <string.h>
#include "sd_card.h"
#include "unity.h"

#define PROBE_PATH SD_CARD_MOUNT_POINT "/test_probe.txt"
#define PROBE_TEXT "nocturne"

TEST_CASE("a file written to the card survives a remount", "[storage][sd]") {
    TEST_ASSERT_EQUAL(ESP_OK, sd_card_mount());

    FILE *file = fopen(PROBE_PATH, "w");
    TEST_ASSERT_NOT_NULL(file);
    fputs(PROBE_TEXT, file);
    TEST_ASSERT_EQUAL(0, fclose(file));

    TEST_ASSERT_EQUAL(ESP_OK, sd_card_unmount());
    TEST_ASSERT_EQUAL(ESP_OK, sd_card_mount());

    char text[sizeof(PROBE_TEXT)] = {0};
    file = fopen(PROBE_PATH, "r");
    TEST_ASSERT_NOT_NULL(file);
    fgets(text, sizeof(text), file);
    fclose(file);
    remove(PROBE_PATH);

    TEST_ASSERT_EQUAL(ESP_OK, sd_card_unmount());
    TEST_ASSERT_EQUAL_STRING(PROBE_TEXT, text);
}
