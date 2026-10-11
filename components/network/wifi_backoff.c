#include "wifi_backoff.h"

uint32_t wifi_backoff_delay_ms(uint32_t attempt) {
    uint32_t delay_ms = WIFI_BACKOFF_FIRST_MS;

    for (uint32_t i = 0; i < attempt && delay_ms < WIFI_BACKOFF_MAX_MS; i++) {
        delay_ms *= 2;
    }

    return delay_ms < WIFI_BACKOFF_MAX_MS ? delay_ms : WIFI_BACKOFF_MAX_MS;
}
