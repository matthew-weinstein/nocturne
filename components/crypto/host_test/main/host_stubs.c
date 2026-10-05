#include <string.h>
#include "esp_mac.h"
#include "esp_timer.h"

static const uint8_t host_stub_mac[] = { 0x02, 0xaa, 0xbb, 0xcc, 0xdd, 0xee };

esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type) {
    (void)type;
    memcpy(mac, host_stub_mac, sizeof(host_stub_mac));
    return ESP_OK;
}

int64_t esp_timer_get_time(void) {
    return 0;
}
