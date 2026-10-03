#include "key_manager.h"

#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "psa/crypto.h"

#define TAG "key_manager"

#define DERIVATION_TASK_STACK_BYTES 8192
#define DERIVATION_TASK_CORE        0
#define KEY_READY_BIT               BIT0
#define KEY_FAILED_BIT              BIT1

#define PBKDF2_ITERATIONS 100000
#define MAC_BYTES         6
#define SALT_SUFFIX       "nocturne"
#define SALT_SUFFIX_BYTES (sizeof(SALT_SUFFIX) - 1)

_Static_assert(KEY_MANAGER_SALT_BYTES == MAC_BYTES + SALT_SUFFIX_BYTES, 
               "salt is the device MAC followed by the literal \"nocturne\"");

static uint8_t device_key[KEY_MANAGER_KEY_BYTES];
static StaticEventGroup_t key_events_storage;
static EventGroupHandle_t key_events;
static bool derivation_started;

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

static void derivation_task(void *arg) {
    const char *passphrase = arg;
    uint8_t salt[KEY_MANAGER_SALT_BYTES];
    esp_err_t status = key_manager_device_salt(salt, sizeof(salt));
    if (status != ESP_OK) {
        ESP_LOGE(TAG, "could not read device MAC: %s", esp_err_to_name(status));
        xEventGroupSetBits(key_events, KEY_FAILED_BIT);
        vTaskDelete(NULL);
    }

    int64_t started_us = esp_timer_get_time();
    status = key_manager_derive(passphrase, salt, sizeof(salt), device_key, sizeof(device_key));
    if (status != ESP_OK) {
        xEventGroupSetBits(key_events, KEY_FAILED_BIT);
        vTaskDelete(NULL);
    }

    ESP_LOGI(TAG, "device key derived from %02x%02x%02x%02x%02x%02x in %lld ms",
             salt[0], salt[1], salt[2], salt[3], salt[4], salt[5],
             (esp_timer_get_time() - started_us) / 1000);

    xEventGroupSetBits(key_events, KEY_READY_BIT);
    vTaskDelete(NULL);
}

esp_err_t key_manager_init(const char *passphrase) {
    if (passphrase == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (derivation_started) {
        return ESP_OK;
    }

    key_events = xEventGroupCreateStatic(&key_events_storage);

    BaseType_t created = xTaskCreatePinnedToCore(derivation_task, "key_derive", DERIVATION_TASK_STACK_BYTES,
                                                 (void *)passphrase, tskIDLE_PRIORITY, NULL, DERIVATION_TASK_CORE);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "could not start the derivation task");
        return ESP_ERR_NO_MEM;
    }

    derivation_started = true;
    return ESP_OK;
}

esp_err_t key_manager_wait_ready(uint32_t timeout_ms) {
    if (!derivation_started) {
        return ESP_ERR_INVALID_STATE;
    }

    EventBits_t bits = xEventGroupWaitBits(key_events, KEY_READY_BIT | KEY_FAILED_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    if (bits & KEY_READY_BIT) {
        return ESP_OK;
    }
    return (bits & KEY_FAILED_BIT) ? ESP_FAIL : ESP_ERR_TIMEOUT;
}

const uint8_t *key_manager_key(void) {
    if (!derivation_started) {
        return NULL;
    }
    return (xEventGroupGetBits(key_events) & KEY_READY_BIT) ? device_key : NULL;
}
