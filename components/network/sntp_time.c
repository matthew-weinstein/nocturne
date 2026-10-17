#include "sntp_time.h"
#include <inttypes.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "sntp_time_fallback.h"

#define NVS_NAMESPACE   "time"
#define NVS_KEY_UNIX    "last_unix"

#define SYNCED_BIT BIT0

static const char *TAG = "sntp_time";

static EventGroupHandle_t sync_events;

static esp_err_t load_last_known(int64_t *out_unix) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_i64(handle, NVS_KEY_UNIX, out_unix);
    nvs_close(handle);
    return err;
}

static esp_err_t store_last_known(int64_t unix_time) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_i64(handle, NVS_KEY_UNIX, unix_time);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

static void restore_from_nvs(void) {
    int64_t last_known_unix;
    esp_err_t err = load_last_known(&last_known_unix);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no stored time (%s), clock unset until sntp", esp_err_to_name(err));
        return;
    }

    int64_t estimate;
    if (!sntp_time_fallback_estimate(last_known_unix, esp_timer_get_time(), (int64_t)time(NULL),
                                     &estimate)) {
        ESP_LOGI(TAG, "clock kept, ahead of stored time %" PRId64, last_known_unix);
        return;
    }

    struct timeval tv = { .tv_sec = (time_t)estimate };
    settimeofday(&tv, NULL);
    ESP_LOGI(TAG, "clock set from stored time %" PRId64 " to %" PRId64 ", unreliable",
             last_known_unix, estimate);
}

static void on_sync(struct timeval *tv) {
    xEventGroupSetBits(sync_events, SYNCED_BIT);

    struct tm local;
    char text[32];
    localtime_r(&tv->tv_sec, &local);
    strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S %Z", &local);
    ESP_LOGI(TAG, "synced: %s", text);

    esp_err_t err = store_last_known((int64_t)tv->tv_sec);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "time not stored: %s", esp_err_to_name(err));
    }
}

esp_err_t sntp_time_init(void) {
    setenv("TZ", CONFIG_NOCTURNE_TIMEZONE, 1);
    tzset();

    sync_events = xEventGroupCreate();
    if (sync_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    restore_from_nvs();

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_NOCTURNE_SNTP_SERVER);
    config.wait_for_sync = false;
    config.sync_cb = on_sync;
    return esp_netif_sntp_init(&config);
}

bool sntp_time_is_reliable(void) {
    return sync_events != NULL && (xEventGroupGetBits(sync_events) & SYNCED_BIT) != 0;
}

esp_err_t sntp_time_wait_reliable(uint32_t timeout_ms) {
    if (sync_events == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    EventBits_t bits = xEventGroupWaitBits(sync_events, SYNCED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
    return (bits & SYNCED_BIT) ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool sntp_time_survived_reset(void) {
    switch (esp_reset_reason()) {
    case ESP_RST_SW:
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
        return true;
    default:
        return false;
    }
}
