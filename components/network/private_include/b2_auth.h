#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define B2_AUTH_TOKEN_MAX      128
#define B2_AUTH_URL_MAX        64
#define B2_AUTH_BUCKET_ID_MAX  32
#define B2_AUTH_UPLOAD_URL_MAX 256
#define B2_AUTH_FILE_ID_MAX    128
#define B2_AUTH_SHA1_HEX_MAX   41
#define B2_AUTH_REUSE_US       (23LL * 60 * 60 * 1000000)

typedef struct {
    char token[B2_AUTH_TOKEN_MAX];
    char api_url[B2_AUTH_URL_MAX];
    char bucket_id[B2_AUTH_BUCKET_ID_MAX];
} b2_auth_t;

typedef struct {
    char url[B2_AUTH_UPLOAD_URL_MAX];
    char token[B2_AUTH_TOKEN_MAX];
} b2_upload_url_t;

typedef struct {
    char file_id[B2_AUTH_FILE_ID_MAX];
    char content_sha1[B2_AUTH_SHA1_HEX_MAX];
} b2_uploaded_file_t;

esp_err_t b2_auth_parse(const char *body, b2_auth_t *out);
esp_err_t b2_auth_parse_upload_url(const char *body, b2_upload_url_t *out);
esp_err_t b2_auth_parse_uploaded_file(const char *body, b2_uploaded_file_t *out);
bool b2_auth_is_fresh(int64_t authorized_us, int64_t now_us);
