#include <string.h>
#include "key_manager.h"
#include "tests.h"
#include "unity.h"

#define KAT_PASSPHRASE "nocturne-test-passphrase"
#define KEY_WAIT_MS    30000

static const uint8_t kat_salt[KEY_MANAGER_SALT_BYTES] = {
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 'n', 'o', 'c', 't', 'u', 'r', 'n', 'e'
};

const uint8_t kat_key[KAT_KEY_BYTES] = {
    0x87, 0x20, 0xff, 0xc8, 0x79, 0x77, 0xb5, 0xad, 0x1a, 0xc6, 0xf5, 0x31, 0x2e, 0x4d, 0x24, 0x10,
    0x0c, 0x2f, 0x50, 0x66, 0xb8, 0xd9, 0xa3, 0x1a, 0x45, 0x4a, 0x31, 0xd0, 0xcd, 0x33, 0x1b, 0x36
};

static const uint8_t stub_device_key[KEY_MANAGER_KEY_BYTES] = {
    0x7c, 0x1e, 0x47, 0x93, 0x86, 0x06, 0xa8, 0x46, 0x48, 0x95, 0x06, 0xff, 0x20, 0xe1, 0xea, 0x7c,
    0x5b, 0xc9, 0x7b, 0x62, 0xb8, 0xc6, 0x50, 0x0b, 0xd1, 0x68, 0xc1, 0xcf, 0x9e, 0x0b, 0xeb, 0xd7
};

static void test_derive_matches_python_hashlib(void) {
    uint8_t key[KEY_MANAGER_KEY_BYTES];
    TEST_ASSERT_EQUAL(ESP_OK, key_manager_derive(KAT_PASSPHRASE, kat_salt, sizeof(kat_salt), key, sizeof(key)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kat_key, key, sizeof(key));
}

static void test_a_different_passphrase_gives_a_different_key(void) {
    uint8_t key[KEY_MANAGER_KEY_BYTES];
    TEST_ASSERT_EQUAL(ESP_OK, key_manager_derive("nocturne-other-passphrase", kat_salt, sizeof(kat_salt),
                                                 key, sizeof(key)));
    TEST_ASSERT_FALSE(memcmp(kat_key, key, sizeof(key)) == 0);
}

static void test_derive_rejects_bad_arguments(void) {
    uint8_t key[KEY_MANAGER_KEY_BYTES];
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, key_manager_derive("", kat_salt, sizeof(kat_salt), key, sizeof(key)));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, key_manager_derive(NULL, kat_salt, sizeof(kat_salt), key, sizeof(key)));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, key_manager_derive(KAT_PASSPHRASE, NULL, 0, key, sizeof(key)));
}

static void test_device_key_is_unavailable_before_init(void) {
    TEST_ASSERT_NULL(key_manager_key());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, key_manager_wait_ready(0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, key_manager_init(NULL));
}

static void test_init_derives_the_device_key_in_the_background(void) {
    TEST_ASSERT_EQUAL(ESP_OK, key_manager_init(KAT_PASSPHRASE));
    TEST_ASSERT_EQUAL(ESP_OK, key_manager_wait_ready(KEY_WAIT_MS));

    const uint8_t *key = key_manager_key();
    TEST_ASSERT_NOT_NULL(key);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(stub_device_key, key, KEY_MANAGER_KEY_BYTES);

    uint8_t salt[KEY_MANAGER_SALT_BYTES];
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, key_manager_device_salt(salt, sizeof(salt) - 1));
}

void run_key_manager_tests(void) {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_derive_matches_python_hashlib);
    RUN_TEST(test_a_different_passphrase_gives_a_different_key);
    RUN_TEST(test_derive_rejects_bad_arguments);
    RUN_TEST(test_device_key_is_unavailable_before_init);
    RUN_TEST(test_init_derives_the_device_key_in_the_background);
}
