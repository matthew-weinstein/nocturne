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
    "  \"sample_shift\": 9,\n"
    "  \"codec\": \"opus\",\n"
    "  \"bitrate\": 32000,\n"
    "  \"opus_application\": \"audio\",\n"
    "  \"encryption\": \"aes-256-gcm\",\n"
    "  \"chunk_frames\": 50,\n"
    "  \"firmware_version\": \"v0.3.2\",\n"
    "  \"time_reliable\": true,\n"
    "  \"resumed\": false,\n"
    "  \"resume_count\": 0,\n"
    "  \"resumes\": [],\n"
    "  \"recovered\": false,\n"
    "  \"muted_ms\": 3340,\n"
    "  \"muted\": [\n"
    "    { \"segment\": 7, \"start_ms\": 142380, \"ms\": 960 },\n"
    "    { \"segment\": 95, \"start_ms\": 287700, \"ms\": 2380 }\n"
    "  ],\n"
    "  \"muted_truncated\": false,\n"
    "  \"health\": {\n"
    "    \"buffer_overflows\": 0,\n"
    "    \"sd_write_errors\": 0,\n"
    "    \"upload_retries\": 3,\n"
    "    \"wifi_disconnects\": 1\n"
    "  }\n"
    "}\n";

static const manifest_mute_t example_mutes[] = {
    { .segment = 7, .start_frame = 7119, .num_frames = 48 },
    { .segment = 95, .start_frame = 14385, .num_frames = 119 },
};

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
        .muted_frames = 167,
        .mutes = example_mutes,
        .num_mutes = 2,
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

static void test_resumes_are_listed_by_segment_and_time(void) {
    static const manifest_resume_t resumes[] = {
        { .segment = 12, .started_unix = 1757208000 },
        { .segment = 40, .started_unix = 1757219000 },
    };
    session_json_t session = example_session();
    session.resume_count = 2;
    session.resumes = resumes;
    session.num_resumes = 2;
    size_t num_bytes = 0;

    TEST_ASSERT_EQUAL(ESP_OK, session_json_format(&session, json, sizeof(json), &num_bytes));
    TEST_ASSERT_NOT_NULL(strstr(json,
                                "  \"resume_count\": 2,\n"
                                "  \"resumes\": [\n"
                                "    { \"segment\": 12, \"started_unix\": 1757208000 },\n"
                                "    { \"segment\": 40, \"started_unix\": 1757219000 }\n"
                                "  ],\n"
                                "  \"recovered\": false,\n"));
}

static void test_a_recovered_session_is_flagged(void) {
    session_json_t session = example_session();
    session.recovered = true;
    size_t num_bytes = 0;

    TEST_ASSERT_EQUAL(ESP_OK, session_json_format(&session, json, sizeof(json), &num_bytes));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"resumes\": [],\n  \"recovered\": true,\n"));
}

static void test_muted_frames_are_reported_in_milliseconds(void) {
    session_json_t session = example_session();
    session.muted_frames = 75;
    size_t num_bytes = 0;

    TEST_ASSERT_EQUAL(ESP_OK, session_json_format(&session, json, sizeof(json), &num_bytes));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"recovered\": false,\n  \"muted_ms\": 1500,\n"));
}

static void test_no_mutes_print_an_empty_list(void) {
    session_json_t session = example_session();
    session.muted_frames = 0;
    session.num_mutes = 0;
    size_t num_bytes = 0;

    TEST_ASSERT_EQUAL(ESP_OK, session_json_format(&session, json, sizeof(json), &num_bytes));
    TEST_ASSERT_NOT_NULL(strstr(json, "  \"muted_ms\": 0,\n  \"muted\": [],\n  \"muted_truncated\": false,\n"));
}

static void test_full_mute_and_resume_lists_fit_the_largest_session_json(void) {
    static manifest_resume_t resumes[MANIFEST_MAX_RESUMES];
    for (int i = 0; i < MANIFEST_MAX_RESUMES; i++) {
        resumes[i] = (manifest_resume_t){
            .segment = MANIFEST_MAX_SEGMENTS - 1,
            .started_unix = UINT32_MAX,
        };
    }
    static manifest_mute_t mutes[MANIFEST_MAX_MUTES];
    for (int i = 0; i < MANIFEST_MAX_MUTES; i++) {
        mutes[i] = (manifest_mute_t){
            .segment = MANIFEST_MAX_SEGMENTS - 1,
            .start_frame = MANIFEST_MAX_MUTE_START,
            .num_frames = MANIFEST_MAX_MUTED_RUN,
        };
    }
    session_json_t session = example_session();
    session.mutes = mutes;
    session.num_mutes = MANIFEST_MAX_MUTES;
    session.mutes_truncated = true;
    session.muted_frames = UINT32_MAX / 20;
    session.resume_count = INT16_MAX;
    session.resumes = resumes;
    session.num_resumes = MANIFEST_MAX_RESUMES;

    static char large[SESSION_JSON_MAX_BYTES];
    size_t num_bytes = 0;
    TEST_ASSERT_EQUAL(ESP_OK, session_json_format(&session, large, sizeof(large), &num_bytes));
    TEST_ASSERT_NOT_NULL(strstr(large, "\"muted_truncated\": true,\n"));
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
    RUN_TEST(test_resumes_are_listed_by_segment_and_time);
    RUN_TEST(test_a_recovered_session_is_flagged);
    RUN_TEST(test_muted_frames_are_reported_in_milliseconds);
    RUN_TEST(test_no_mutes_print_an_empty_list);
    RUN_TEST(test_full_mute_and_resume_lists_fit_the_largest_session_json);
    RUN_TEST(test_large_and_negative_times_are_printed_whole);
    RUN_TEST(test_quotes_backslashes_and_control_characters_are_escaped);
    RUN_TEST(test_a_buffer_one_byte_short_is_rejected);
}
