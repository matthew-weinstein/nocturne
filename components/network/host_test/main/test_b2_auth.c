#include <stdint.h>
#include <stdio.h>
#include "b2_auth.h"
#include "tests.h"
#include "unity.h"

#define HOUR_US (60LL * 60 * 1000000)

static const char AUTHORIZE_RESPONSE[] =
    "{\n"
    "  \"accountId\": \"0123456789ab\",\n"
    "  \"apiInfo\": {\n"
    "    \"storageApi\": {\n"
    "      \"absoluteMinimumPartSize\": 5000000,\n"
    "      \"apiUrl\": \"https://api005.backblazeb2.com\",\n"
    "      \"bucketId\": \"4a48fe8875c6214145260818\",\n"
    "      \"bucketName\": \"nocturne-sessions\",\n"
    "      \"capabilities\": [\"writeFiles\"],\n"
    "      \"downloadUrl\": \"https://f005.backblazeb2.com\",\n"
    "      \"infoType\": \"storageApi\",\n"
    "      \"namePrefix\": null,\n"
    "      \"recommendedPartSize\": 100000000,\n"
    "      \"s3ApiUrl\": \"https://s3.us-west-002.backblazeb2.com\"\n"
    "    }\n"
    "  },\n"
    "  \"applicationKeyExpirationTimestamp\": null,\n"
    "  \"authorizationToken\": \"4_0050123456789ab0000000001_01a2b3c4_d5e6f7_acct_abcDEF123=\"\n"
    "}\n";

static void test_a_v3_response_yields_token_api_url_and_bucket(void) {
    b2_auth_t auth;
    TEST_ASSERT_EQUAL(ESP_OK, b2_auth_parse(AUTHORIZE_RESPONSE, &auth));
    TEST_ASSERT_EQUAL_STRING("4_0050123456789ab0000000001_01a2b3c4_d5e6f7_acct_abcDEF123=",
                             auth.token);
    TEST_ASSERT_EQUAL_STRING("https://api005.backblazeb2.com", auth.api_url);
    TEST_ASSERT_EQUAL_STRING("4a48fe8875c6214145260818", auth.bucket_id);
}

static void test_escaped_slashes_are_unescaped(void) {
    b2_auth_t auth;
    const char *body = "{\"authorizationToken\":\"t\",\"apiUrl\":\"https:\\/\\/api.example\","
                       "\"bucketId\":\"b\"}";
    TEST_ASSERT_EQUAL(ESP_OK, b2_auth_parse(body, &auth));
    TEST_ASSERT_EQUAL_STRING("https://api.example", auth.api_url);
}

static void test_a_key_named_inside_a_value_is_skipped(void) {
    b2_auth_t auth;
    const char *body = "{\"label\":\"apiUrl\",\"authorizationToken\":\"t\","
                       "\"apiUrl\":\"https://api.example\",\"bucketId\":\"b\"}";
    TEST_ASSERT_EQUAL(ESP_OK, b2_auth_parse(body, &auth));
    TEST_ASSERT_EQUAL_STRING("https://api.example", auth.api_url);
}

static void test_a_key_not_scoped_to_one_bucket_is_rejected(void) {
    b2_auth_t auth;
    const char *body = "{\"authorizationToken\":\"t\",\"apiUrl\":\"https://api.example\","
                       "\"bucketId\": null}";
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, b2_auth_parse(body, &auth));
}

static void test_a_missing_token_is_rejected(void) {
    b2_auth_t auth;
    const char *body = "{\"apiUrl\":\"https://api.example\",\"bucketId\":\"b\"}";
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, b2_auth_parse(body, &auth));
}

static void test_an_error_body_is_rejected(void) {
    b2_auth_t auth;
    const char *body = "{\"code\":\"unauthorized\",\"message\":\"\",\"status\":401}";
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, b2_auth_parse(body, &auth));
}

static void test_an_oversized_value_is_rejected(void) {
    b2_auth_t auth;
    char body[256];
    snprintf(body, sizeof(body),
             "{\"authorizationToken\":\"t\",\"apiUrl\":\"https://%s\",\"bucketId\":\"b\"}",
             "a-host-name-long-enough-to-overflow-the-api-url-field.example");
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, b2_auth_parse(body, &auth));
}

static void test_an_unterminated_value_is_rejected(void) {
    b2_auth_t auth;
    const char *body = "{\"authorizationToken\":\"t\",\"apiUrl\":\"https://api.exa";
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE, b2_auth_parse(body, &auth));
}

static void test_an_unsupported_escape_is_rejected(void) {
    b2_auth_t auth;
    const char *body = "{\"authorizationToken\":\"t\\u0041\",\"apiUrl\":\"u\",\"bucketId\":\"b\"}";
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE, b2_auth_parse(body, &auth));
}

static void test_an_empty_value_is_rejected(void) {
    b2_auth_t auth;
    const char *body = "{\"authorizationToken\":\"\",\"apiUrl\":\"u\",\"bucketId\":\"b\"}";
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE, b2_auth_parse(body, &auth));
}

static void test_authorization_is_reused_for_23_hours(void) {
    int64_t authorized_us = 5 * HOUR_US;
    TEST_ASSERT_TRUE(b2_auth_is_fresh(authorized_us, authorized_us));
    TEST_ASSERT_TRUE(b2_auth_is_fresh(authorized_us, authorized_us + B2_AUTH_REUSE_US - 1));
    TEST_ASSERT_FALSE(b2_auth_is_fresh(authorized_us, authorized_us + B2_AUTH_REUSE_US));
    TEST_ASSERT_EQUAL_INT64(23 * HOUR_US, B2_AUTH_REUSE_US);
}

static void test_a_time_before_authorization_is_not_fresh(void) {
    TEST_ASSERT_FALSE(b2_auth_is_fresh(HOUR_US, HOUR_US - 1));
}

void run_b2_auth_tests(void) {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_a_v3_response_yields_token_api_url_and_bucket);
    RUN_TEST(test_escaped_slashes_are_unescaped);
    RUN_TEST(test_a_key_named_inside_a_value_is_skipped);
    RUN_TEST(test_a_key_not_scoped_to_one_bucket_is_rejected);
    RUN_TEST(test_a_missing_token_is_rejected);
    RUN_TEST(test_an_error_body_is_rejected);
    RUN_TEST(test_an_oversized_value_is_rejected);
    RUN_TEST(test_an_unterminated_value_is_rejected);
    RUN_TEST(test_an_unsupported_escape_is_rejected);
    RUN_TEST(test_an_empty_value_is_rejected);
    RUN_TEST(test_authorization_is_reused_for_23_hours);
    RUN_TEST(test_a_time_before_authorization_is_not_fresh);
}
