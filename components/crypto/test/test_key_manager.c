#include <string.h>
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "kat.h"
#include "key_manager.h"
#include "unity.h"

#define KEY_WAIT_MS       30000
#define MAC_BYTES         6
#define SALT_SUFFIX       "nocturne"
#define SALT_SUFFIX_BYTES (sizeof(SALT_SUFFIX) - 1)

static void derive_at_idle_priority(const char *passphrase, const uint8_t *salt, uint8_t *out_key) {
    UBaseType_t priority = uxTaskPriorityGet(NULL);
    vTaskPrioritySet(NULL, tskIDLE_PRIORITY);
    esp_err_t status = key_manager_derive(passphrase, salt, KEY_MANAGER_SALT_BYTES, out_key, KEY_MANAGER_KEY_BYTES);
    vTaskPrioritySet(NULL, priority);
    TEST_ASSERT_EQUAL(ESP_OK, status);
}

TEST_CASE("PBKDF2 on the hardware SHA matches Python hashlib", "[crypto]") {
    uint8_t key[KEY_MANAGER_KEY_BYTES];
    derive_at_idle_priority(KAT_PASSPHRASE, kat_salt, key);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kat_key, key, sizeof(key));
}

TEST_CASE("the device key is salted with this chip's MAC", "[crypto]") {
    uint8_t mac[MAC_BYTES];
    TEST_ASSERT_EQUAL(ESP_OK, esp_read_mac(mac, ESP_MAC_WIFI_STA));

    uint8_t salt[KEY_MANAGER_SALT_BYTES];
    TEST_ASSERT_EQUAL(ESP_OK, key_manager_device_salt(salt, sizeof(salt)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(mac, salt, MAC_BYTES);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(SALT_SUFFIX, salt + MAC_BYTES, SALT_SUFFIX_BYTES);

    TEST_ASSERT_EQUAL(ESP_OK, key_manager_init(KAT_PASSPHRASE));
    TEST_ASSERT_EQUAL(ESP_OK, key_manager_wait_ready(KEY_WAIT_MS));
    TEST_ASSERT_NOT_NULL(key_manager_key());

    uint8_t expected[KEY_MANAGER_KEY_BYTES];
    derive_at_idle_priority(KAT_PASSPHRASE, salt, expected);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, key_manager_key(), KEY_MANAGER_KEY_BYTES);
}
