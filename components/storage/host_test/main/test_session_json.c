#include <string.h>
#include "session_json.h"
#include "tests.h"
#include "unity.h"

static const char expected[] =
    "{\n"
    "  \"session_id\": \"20260906_2312\",\n"
    "  \"device_id\": \"44b176a468c8\",\n"
    "  \"started_unix\": 1757203920,\n"
    "  \"ended_unix\": 1757236800,\n"
    "  \"segment_count\": 96,\n"
    "  \"sample_rate\": 16000,\n"
    "  \"channels\": 1,\n"
    "  \"codec\": \"opus\",\n"
    "  \"bitrate\": 32000,\n"
    "  \"opus_application\": \"audio\",\n"
    "  \"encryption\": \"aes-256-gcm\",\n"
    "  \"chunk_frames\": 50,\n"
    "  \"firmware_version\": \"v0.3.2\",\n"
    "  \"time_reliable\": true,\n"
    "  \"resumed\": false,\n"
    "  \"resume_count\": 0,\n"
    "  \"health\": {\n"
    "    \"buffer_overflows\": 0,\n"
    "    \"sd_write_errors\": 0,\n"
    "    \"upload_retries\": 3,\n"
    "    \"wifi_disconnects\": 1\n"
    "  }\n"
    "}\n";

static char json[1024];

static session_json_t example_session(void) {
    session_json_t session = {
        .session_id = "20260906_2312",
        .device_id = "44b176a468c8",
        .firmware_version = "v0.3.2",
        .opus_application = "audio",
        .started_unix = 1757203920,
        .ended_unix = 1757236800,
        .segment_count = 96,
        .bitrate = 32000,
        .time_reliable = true,
        .upload_retries = 3,
        .wifi_disconnects = 1,
    };
    return session;
}

static void test_matches_the_design_doc_example(void) {
    session_json_t session = example_session();
    size_t num_bytes = 0;

    TEST_ASSERT_EQUAL(ESP_OK, session_json_format(&session, json, sizeof(json), &num_bytes));
    TEST_ASSERT_EQUAL_STRING(expected, json);
    TEST_ASSERT_EQUAL_size_t(strlen(expected), num_bytes);
}

static void test_resumed_follows_resume_count(void) {
    session_json_t session = example_session();
    session.resume_count = 2;
    size_t num_bytes = 0;

    TEST_ASSERT_EQUAL(ESP_OK, session_json_format(&session, json, sizeof(json), &num_bytes));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"resumed\": true,\n"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"resume_count\": 2,\n"));
}

static void test_large_and_negative_times_are_printed_whole(void) {
    session_json_t session = example_session();
    session.started_unix = -1;
    session.ended_unix = 4102444800LL;
    session.time_reliable = false;
    size_t num_bytes = 0;

    TEST_ASSERT_EQUAL(ESP_OK, session_json_format(&session, json, sizeof(json), &num_bytes));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"started_unix\": -1,\n"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"ended_unix\": 4102444800,\n"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"time_reliable\": false,\n"));
}

static void test_quotes_backslashes_and_control_characters_are_escaped(void) {
    session_json_t session = example_session();
    session.firmware_version = "v1 \"rc\\2\"\n";
    size_t num_bytes = 0;

    TEST_ASSERT_EQUAL(ESP_OK, session_json_format(&session, json, sizeof(json), &num_bytes));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"firmware_version\": \"v1 \\\"rc\\\\2\\\"\\u000a\",\n"));
}

static void test_a_buffer_one_byte_short_is_rejected(void) {
    session_json_t session = example_session();
    size_t num_bytes = 99;

    TEST_ASSERT_EQUAL(ESP_OK, session_json_format(&session, json, strlen(expected) + 1, &num_bytes));

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, session_json_format(&session, json, strlen(expected), &num_bytes));
    TEST_ASSERT_EQUAL_size_t(0, num_bytes);
    TEST_ASSERT_EQUAL_STRING("", json);

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, session_json_format(&session, json, 0, &num_bytes));
}

void run_session_json_tests(void) {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_matches_the_design_doc_example);
    RUN_TEST(test_resumed_follows_resume_count);
    RUN_TEST(test_large_and_negative_times_are_printed_whole);
    RUN_TEST(test_quotes_backslashes_and_control_characters_are_escaped);
    RUN_TEST(test_a_buffer_one_byte_short_is_rejected);
}
