#pragma once

#include <stdint.h>

#define WIFI_BACKOFF_FIRST_MS 1000
#define WIFI_BACKOFF_MAX_MS   60000

uint32_t wifi_backoff_delay_ms(uint32_t attempt);
