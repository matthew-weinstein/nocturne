#include "b2_auth.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const char *skip_space(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    return p;
}

static const char *find_value(const char *body, const char *key) {
    char quoted[32];
    snprintf(quoted, sizeof(quoted), "\"%s\"", key);

    for (const char *p = strstr(body, quoted); p != NULL; p = strstr(p + 1, quoted)) {
        const char *after = skip_space(p + strlen(quoted));
        if (*after == ':') {
            return skip_space(after + 1);
        }
    }

    return NULL;
}

static esp_err_t copy_string(const char *value, char *out, size_t max) {
    if (*value != '"') {
        return ESP_ERR_NOT_FOUND;
    }

    size_t len = 0;
    for (const char *p = value + 1; *p != '"'; p++) {
        char c = *p;
        if (c == '\0') {
            return ESP_ERR_INVALID_RESPONSE;
        }

        if (c == '\\') {
            c = *++p;
            if (c != '"' && c != '\\' && c != '/') {
                return ESP_ERR_INVALID_RESPONSE;
            }
        }

        if (len + 1 >= max) {
            return ESP_ERR_INVALID_SIZE;
        }
        out[len++] = c;
    }

    if (len == 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    out[len] = '\0';
    return ESP_OK;
}

static esp_err_t read_field(const char *body, const char *key, char *out, size_t max) {
    const char *value = find_value(body, key);
    if (value == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    return copy_string(value, out, max);
}

esp_err_t b2_auth_parse(const char *body, b2_auth_t *out) {
    esp_err_t err = read_field(body, "authorizationToken", out->token, sizeof(out->token));
    if (err == ESP_OK) {
        err = read_field(body, "apiUrl", out->api_url, sizeof(out->api_url));
    }
    if (err == ESP_OK) {
        err = read_field(body, "bucketId", out->bucket_id, sizeof(out->bucket_id));
    }
    return err;
}

bool b2_auth_is_fresh(int64_t authorized_us, int64_t now_us) {
    int64_t age_us = now_us - authorized_us;
    return age_us >= 0 && age_us < B2_AUTH_REUSE_US;
}
