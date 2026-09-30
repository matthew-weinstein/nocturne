#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define KEY_MANAGER_KEY_BYTES  32
#define KEY_MANAGER_SALT_BYTES 14

esp_err_t key_manager_init(void);
const uint8_t *key_manager_key(void);

esp_err_t key_manager_device_salt(uint8_t *out, size_t out_len);
esp_err_t key_manager_derive(const char *passphrase, const uint8_t *salt, size_t salt_len, uint8_t *out_key, size_t out_key_len);
