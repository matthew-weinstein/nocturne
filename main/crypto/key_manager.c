#include "key_manager.h"

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "psa/crypto.h"
#include "secrets.h"

#define TAG "key_manager"

#define PBKDF2_ITERATIONS 100000
#define MAC_BYTES         6
#define SALT_SUFFIX       "nocturne"
#define SALT_SUFFIX_BYTES (sizeof(SALT_SUFFIX) - 1)

_Static_assert(KEY_MANAGER_SALT_BYTES == MAC_BYTES + SALT_SUFFIX_BYTES, 
               "salt is the device MAC followed by the literal \"nocturne\"");

static uint8_t device_key[KEY_MANAGER_KEY_BYTES];
static bool device_key_ready;

esp_err_t key_manager_device_salt(uint8_t *out, size_t out_len) {
    if (out == NULL || out_len != KEY_MANAGER_SALT_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t status = esp_read_mac(out, ESP_MAC_WIFI_STA);
    if (status != ESP_OK) {
        return status;
    }

    memcpy(out + MAC_BYTES, SALT_SUFFIX, SALT_SUFFIX_BYTES);
    return ESP_OK;
}

esp_err_t key_manager_derive(const char *passphrase, const uint8_t *salt, size_t salt_len, uint8_t *out_key, size_t out_key_len) {
    if (passphrase == NULL || passphrase[0] == '\0' || salt == NULL || salt_len == 0 ||
        out_key == NULL || out_key_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    psa_key_derivation_operation_t operation = PSA_KEY_DERIVATION_OPERATION_INIT;

    psa_status_t status = psa_key_derivation_setup(&operation, PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_256));
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_input_integer(&operation, PSA_KEY_DERIVATION_INPUT_COST, PBKDF2_ITERATIONS);
    }
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SALT, salt, salt_len);
    }
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_PASSWORD,
                                                (const uint8_t *)passphrase, strlen(passphrase));
    }
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_output_bytes(&operation, out_key, out_key_len);
    }

    psa_key_derivation_abort(&operation);

    if (status != PSA_SUCCESS) {
        memset(out_key, 0, out_key_len);
        ESP_LOGE(TAG, "PBKDF2 failed with PSA status %d", (int)status);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t key_manager_init(void) {
    if (device_key_ready) {
        return ESP_OK;
    }

    uint8_t salt[KEY_MANAGER_SALT_BYTES];
    esp_err_t status = key_manager_device_salt(salt, sizeof(salt));
    if (status != ESP_OK) {
        ESP_LOGE(TAG, "could not read device MAC: %s", esp_err_to_name(status));
        return status;
    }

    int64_t started_us = esp_timer_get_time();
    status = key_manager_derive(NOCTURNE_PASSPHRASE, salt, sizeof(salt), device_key, sizeof(device_key));
    if (status != ESP_OK) {
        return status;
    }

    ESP_LOGI(TAG, "device key derived from %02x%02x%02x%02x%02x%02x in %lld ms",
             salt[0], salt[1], salt[2], salt[3], salt[4], salt[5],
             (esp_timer_get_time() - started_us) / 1000);

    device_key_ready = true;
    return ESP_OK;
}

const uint8_t *key_manager_key(void) {
    return device_key_ready ? device_key : NULL;
}
