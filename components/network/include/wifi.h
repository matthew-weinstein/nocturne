#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t wifi_init(const char *ssid, const char *password);
esp_err_t wifi_wait_connected(uint32_t timeout_ms);
bool wifi_is_connected(void);
uint32_t wifi_disconnect_count(void);
