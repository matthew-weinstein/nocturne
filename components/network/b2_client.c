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
#include "sha1_stream.h"

#define AUTHORIZE_URL            "https://api.backblazeb2.com/b2api/v4/b2_authorize_account"
#define UPLOAD_URL_PATH          "/b2api/v4/b2_get_upload_url?bucketId="
#define HTTP_TIMEOUT_MS          15000
#define HTTP_STATUS_OK           200
#define HTTP_STATUS_UNAUTHORIZED 401
#define REQUEST_URL_MAX          (B2_AUTH_URL_MAX + sizeof(UPLOAD_URL_PATH) + B2_AUTH_BUCKET_ID_MAX)
#define CREDENTIALS_MAX          128
#define BASIC_PREFIX             "Basic "
#define BASIC_HEADER_MAX         (sizeof(BASIC_PREFIX) + 4 * (CREDENTIALS_MAX / 3 + 1))
#define RESPONSE_MAX             2048
#define BODY_BLOCK_BYTES         4096

static const char *TAG = "b2_client";

static char basic_header[BASIC_HEADER_MAX];
static char response[RESPONSE_MAX];
static char body_block[BODY_BLOCK_BYTES];
static b2_auth_t auth;
static int64_t authorized_us;
static bool authorized;
static b2_upload_url_t upload_url;
static bool has_upload_url;

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
    has_upload_url = false;
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

static esp_err_t request_get(const char *url, const char *authorization, int *out_status) {
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_set_header(client, "Authorization", authorization);
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
    esp_err_t err = request_get(AUTHORIZE_URL, basic_header, &status);
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

esp_err_t b2_client_get_upload_url(void) {
    esp_err_t err = b2_client_authorize();
    if (err != ESP_OK) {
        return err;
    }

    if (has_upload_url) {
        return ESP_OK;
    }

    char url[REQUEST_URL_MAX];
    snprintf(url, sizeof(url), "%s" UPLOAD_URL_PATH "%s", auth.api_url, auth.bucket_id);

    int status;
    err = request_get(url, auth.token, &status);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "upload url request failed: %s", esp_err_to_name(err));
        return err;
    }

    if (status == HTTP_STATUS_UNAUTHORIZED) {
        authorized = false;
    }

    if (status != HTTP_STATUS_OK) {
        ESP_LOGE(TAG, "upload url refused, status %d: %s", status, response);
        return ESP_FAIL;
    }

    err = b2_auth_parse_upload_url(response, &upload_url);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "upload url response unreadable: %s", esp_err_to_name(err));
        return err;
    }

    has_upload_url = true;
    ESP_LOGI(TAG, "upload url %s", upload_url.url);
    return ESP_OK;
}

static esp_err_t write_all(esp_http_client_handle_t client, const char *data, size_t num_bytes) {
    while (num_bytes > 0) {
        int written = esp_http_client_write(client, data, (int)num_bytes);
        if (written <= 0) {
            return ESP_FAIL;
        }
        data += written;
        num_bytes -= (size_t)written;
    }
    return ESP_OK;
}

static esp_err_t write_body(esp_http_client_handle_t client, const char *path, size_t num_bytes) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = ESP_OK;
    while (err == ESP_OK && num_bytes > 0) {
        size_t block = num_bytes < sizeof(body_block) ? num_bytes : sizeof(body_block);
        if (fread(body_block, 1, block, file) != block) {
            err = ESP_FAIL;
            break;
        }
        err = write_all(client, body_block, block);
        num_bytes -= block;
    }

    fclose(file);
    return err;
}

static esp_err_t request_upload(const char *path, const char *file_name, const char *sha1_hex,
                                size_t num_bytes, int *out_status) {
    esp_http_client_config_t config = {
        .url = upload_url.url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_set_header(client, "Authorization", upload_url.token);
    if (err == ESP_OK) {
        err = esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    }
    if (err == ESP_OK) {
        err = esp_http_client_set_header(client, "X-Bz-File-Name", file_name);
    }
    if (err == ESP_OK) {
        err = esp_http_client_set_header(client, "X-Bz-Content-Sha1", sha1_hex);
    }
    if (err == ESP_OK) {
        err = esp_http_client_open(client, (int)num_bytes);
    }
    if (err == ESP_OK) {
        err = write_body(client, path, num_bytes);
    }
    if (err == ESP_OK) {
        err = read_response(client, out_status);
    }

    esp_http_client_cleanup(client);
    return err;
}

esp_err_t b2_client_upload_file(const char *path, const char *file_name) {
    char sha1_hex[SHA1_STREAM_HEX_LEN];
    size_t num_bytes;
    esp_err_t err = sha1_stream_file(path, sha1_hex, sizeof(sha1_hex), &num_bytes);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "could not hash %s: %s", path, esp_err_to_name(err));
        return err;
    }

    err = b2_client_get_upload_url();
    if (err != ESP_OK) {
        return err;
    }

    has_upload_url = false;

    int status;
    err = request_upload(path, file_name, sha1_hex, num_bytes, &status);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "upload of %s failed: %s", file_name, esp_err_to_name(err));
        return err;
    }

    if (status != HTTP_STATUS_OK) {
        ESP_LOGE(TAG, "upload of %s refused, status %d: %s", file_name, status, response);
        return ESP_FAIL;
    }

    b2_uploaded_file_t uploaded;
    err = b2_auth_parse_uploaded_file(response, &uploaded);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "upload response unreadable: %s", esp_err_to_name(err));
        return err;
    }

    if (strcmp(uploaded.content_sha1, sha1_hex) != 0) {
        ESP_LOGE(TAG, "upload of %s stored sha1 %s, sent %s", file_name, uploaded.content_sha1, sha1_hex);
        return ESP_ERR_INVALID_CRC;
    }

    has_upload_url = true;
    ESP_LOGI(TAG, "uploaded %s, %u bytes, sha1 %s, file id %s", file_name, (unsigned)num_bytes,
             sha1_hex, uploaded.file_id);
    return ESP_OK;
}
