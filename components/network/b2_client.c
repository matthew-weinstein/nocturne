#include "b2_client.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "b2_auth.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mbedtls/base64.h"

#define AUTHORIZE_URL     "https://api.backblazeb2.com/b2api/v4/b2_authorize_account"
#define HTTP_TIMEOUT_MS   15000
#define HTTP_STATUS_OK    200
#define CREDENTIALS_MAX   128
#define BASIC_PREFIX      "Basic "
#define BASIC_HEADER_MAX  (sizeof(BASIC_PREFIX) + 4 * (CREDENTIALS_MAX / 3 + 1))
#define RESPONSE_MAX      2048

static const char *TAG = "b2_client";

static char basic_header[BASIC_HEADER_MAX];
static char response[RESPONSE_MAX];
static b2_auth_t auth;
static int64_t authorized_us;
static bool authorized;

esp_err_t b2_client_init(const char *key_id, const char *application_key) {
    char credentials[CREDENTIALS_MAX];
    int len = snprintf(credentials, sizeof(credentials), "%s:%s", key_id, application_key);
    if (len < 0 || (size_t)len >= sizeof(credentials)) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t prefix_len = strlen(BASIC_PREFIX);
    memcpy(basic_header, BASIC_PREFIX, prefix_len);

    size_t encoded_len;
    int ret = mbedtls_base64_encode((unsigned char *)basic_header + prefix_len,
                                    sizeof(basic_header) - prefix_len, &encoded_len,
                                    (const unsigned char *)credentials, (size_t)len);
    memset(credentials, 0, sizeof(credentials));

    authorized = false;
    if (ret != 0) {
        basic_header[0] = '\0';
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static esp_err_t read_response(esp_http_client_handle_t client, int *out_status) {
    if (esp_http_client_fetch_headers(client) < 0) {
        return ESP_FAIL;
    }

    int len = esp_http_client_read_response(client, response, sizeof(response) - 1);
    if (len < 0) {
        return ESP_FAIL;
    }

    if (!esp_http_client_is_complete_data_received(client)) {
        return ESP_ERR_INVALID_SIZE;
    }

    response[len] = '\0';
    *out_status = esp_http_client_get_status_code(client);
    return ESP_OK;
}

static esp_err_t request_authorization(int *out_status) {
    esp_http_client_config_t config = {
        .url = AUTHORIZE_URL,
        .method = HTTP_METHOD_GET,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_set_header(client, "Authorization", basic_header);
    if (err == ESP_OK) {
        err = esp_http_client_open(client, 0);
    }
    if (err == ESP_OK) {
        err = read_response(client, out_status);
    }

    esp_http_client_cleanup(client);
    return err;
}

esp_err_t b2_client_authorize(void) {
    if (basic_header[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    int64_t now_us = esp_timer_get_time();
    if (authorized && b2_auth_is_fresh(authorized_us, now_us)) {
        return ESP_OK;
    }

    authorized = false;

    int status;
    esp_err_t err = request_authorization(&status);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "authorize request failed: %s", esp_err_to_name(err));
        return err;
    }

    if (status != HTTP_STATUS_OK) {
        ESP_LOGE(TAG, "authorize refused, status %d: %s", status, response);
        return ESP_FAIL;
    }

    err = b2_auth_parse(response, &auth);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "authorize response unreadable: %s", esp_err_to_name(err));
        return err;
    }

    authorized_us = now_us;
    authorized = true;
    ESP_LOGI(TAG, "authorized, api %s, bucket %s", auth.api_url, auth.bucket_id);
    return ESP_OK;
}
