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

static const char *find_value(const char *from, const char *key) {
    char quoted[32];
    snprintf(quoted, sizeof(quoted), "\"%s\"", key);

    for (const char *p = strstr(from, quoted); p != NULL; p = strstr(p + 1, quoted)) {
        const char *after = skip_space(p + strlen(quoted));
        if (*after == ':') {
            return skip_space(after + 1);
        }
    }

    return NULL;
}

static const char *skip_string(const char *p) {
    for (p++; *p != '"'; p++) {
        if (*p == '\0') {
            return NULL;
        }
        if (*p == '\\' && *++p == '\0') {
            return NULL;
        }
    }
    return p + 1;
}

static esp_err_t count_objects(const char *array, int *out_count, const char **out_end) {
    int depth = 0;
    int count = 0;

    for (const char *p = array; *p != '\0';) {
        if (*p == '"') {
            p = skip_string(p);
            if (p == NULL) {
                return ESP_ERR_INVALID_RESPONSE;
            }
            continue;
        }

        if (*p == '[' || *p == '{') {
            depth++;
            if (*p == '{' && depth == 2) {
                count++;
            }
        } else if (*p == ']' || *p == '}') {
            depth--;
            if (depth == 0) {
                *out_count = count;
                *out_end = p;
                return ESP_OK;
            }
        }
        p++;
    }

    return ESP_ERR_INVALID_RESPONSE;
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

static esp_err_t read_bucket_id(const char *body, char *out, size_t max) {
    const char *buckets = find_value(body, "buckets");
    if (buckets == NULL || *buckets != '[') {
        return ESP_ERR_NOT_FOUND;
    }

    int num_buckets;
    const char *end;
    esp_err_t err = count_objects(buckets, &num_buckets, &end);
    if (err != ESP_OK) {
        return err;
    }

    if (num_buckets == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    if (num_buckets > 1) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    const char *id = find_value(buckets, "id");
    if (id == NULL || id > end) {
        return ESP_ERR_NOT_FOUND;
    }
    return copy_string(id, out, max);
}

esp_err_t b2_auth_parse(const char *body, b2_auth_t *out) {
    esp_err_t err = read_field(body, "authorizationToken", out->token, sizeof(out->token));
    if (err == ESP_OK) {
        err = read_field(body, "apiUrl", out->api_url, sizeof(out->api_url));
    }
    if (err == ESP_OK) {
        err = read_bucket_id(body, out->bucket_id, sizeof(out->bucket_id));
    }
    return err;
}

esp_err_t b2_auth_parse_upload_url(const char *body, b2_upload_url_t *out) {
    esp_err_t err = read_field(body, "uploadUrl", out->url, sizeof(out->url));
    if (err == ESP_OK) {
        err = read_field(body, "authorizationToken", out->token, sizeof(out->token));
    }
    return err;
}

bool b2_auth_is_fresh(int64_t authorized_us, int64_t now_us) {
    int64_t age_us = now_us - authorized_us;
    return age_us >= 0 && age_us < B2_AUTH_REUSE_US;
}
