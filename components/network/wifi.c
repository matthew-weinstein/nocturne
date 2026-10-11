#include "wifi.h"
#include <inttypes.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "wifi_backoff.h"

#define WIFI_CONNECTED_BIT BIT0

static const char *TAG = "wifi";

static EventGroupHandle_t wifi_events;
static esp_timer_handle_t reconnect_timer;
static uint32_t num_attempts;
static uint32_t num_disconnects;

static void try_connect(void *arg) {
    (void)arg;

    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "connect failed: %s", esp_err_to_name(err));
    }
}

static void schedule_reconnect(void) {
    uint32_t delay_ms = wifi_backoff_delay_ms(num_attempts);
    num_attempts++;

    ESP_LOGI(TAG, "reconnecting in %" PRIu32 " ms (attempt %" PRIu32 ")", delay_ms, num_attempts);
    esp_timer_stop(reconnect_timer);

    esp_err_t err = esp_timer_start_once(reconnect_timer, (uint64_t)delay_ms * 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "reconnect not scheduled: %s", esp_err_to_name(err));
    }
}

static void on_disconnected(const wifi_event_sta_disconnected_t *event) {
    if (xEventGroupGetBits(wifi_events) & WIFI_CONNECTED_BIT) {
        num_disconnects++;
        xEventGroupClearBits(wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "connection lost, reason %d", event->reason);
    } else {
        ESP_LOGD(TAG, "connect attempt failed, reason %d", event->reason);
    }

    schedule_reconnect();
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        try_connect(NULL);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        on_disconnected(data);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        ESP_LOGI(TAG, "got ip " IPSTR, IP2STR(&event->ip_info.ip));
        num_attempts = 0;
        xEventGroupSetBits(wifi_events, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_init(const char *ssid, const char *password) {
    wifi_config_t config = {
        .sta.threshold.authmode = WIFI_AUTH_WPA2_PSK,
    };

    if (strlen(ssid) >= sizeof(config.sta.ssid) || strlen(password) >= sizeof(config.sta.password)) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(config.sta.ssid, ssid, strlen(ssid));
    memcpy(config.sta.password, password, strlen(password));

    wifi_events = xEventGroupCreate();
    if (wifi_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const esp_timer_create_args_t timer_args = {
        .callback = try_connect,
        .name = "wifi_reconnect",
    };

    esp_err_t err = esp_timer_create(&timer_args, &reconnect_timer);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_netif_init();
    if (err != ESP_OK) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK) {
        return err;
    }

    if (esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_FAIL;
    }

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_config);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL, NULL);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL, NULL);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "connecting to %s", ssid);
    return esp_wifi_start();
}

esp_err_t wifi_wait_connected(uint32_t timeout_ms) {
    if (wifi_events == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    EventBits_t bits = xEventGroupWaitBits(wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(timeout_ms));
    return (bits & WIFI_CONNECTED_BIT) ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool wifi_is_connected(void) {
    return wifi_events != NULL && (xEventGroupGetBits(wifi_events) & WIFI_CONNECTED_BIT);
}

uint32_t wifi_disconnect_count(void) {
    return num_disconnects;
}
