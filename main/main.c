#include <inttypes.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "secrets.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "sd_card.h"
#include "recorder.h"
#include "sdkconfig.h"
#include "system_check.h"
#include "key_manager.h"
#include "led.h"
#include "button.h"
#include "supervisor.h"
#include "health.h"

#define KEY_READY_TIMEOUT_MS 30000
#define STATE_POLL_MS        100

static const char *TAG = "nocturne";
static EventGroupHandle_t s_wifi_events;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retries = 0;

#define OTA_URL "https://github.com/matthew-weinstein/nocturne/releases/latest/download/nocturne.bin"

static void do_ota(void)
{
    esp_http_client_config_t http_cfg = {
        .url = OTA_URL,
        .timeout_ms = 30000,
        .keep_alive_enable = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_https_ota_handle_t handle = NULL;

    esp_err_t err = esp_https_ota_begin(&ota_cfg, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ota begin failed: %s", esp_err_to_name(err));
        return;
    }

    esp_app_desc_t incoming;
    err = esp_https_ota_get_img_desc(handle, &incoming);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "could not read image header: %s", esp_err_to_name(err));
        esp_https_ota_abort(handle);
        return;
    }

    const esp_app_desc_t *current = esp_app_get_description();
    ESP_LOGI(TAG, "running %s, server has %s", current->version, incoming.version);

    if (strcmp(incoming.version, current->version) == 0) {
        ESP_LOGI(TAG, "already up to date");
        esp_https_ota_abort(handle);
        return;
    }

    ESP_LOGI(TAG, "updating to %s", incoming.version);

    while (1) {
        err = esp_https_ota_perform(handle);

        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        
        ESP_LOGI(TAG, "%d bytes read", esp_https_ota_get_image_len_read(handle));
    }

    if (!esp_https_ota_is_complete_data_received(handle)) {
        ESP_LOGE(TAG, "incomplete download");
        esp_https_ota_abort(handle);
        return;
    }

    err = esp_https_ota_finish(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "update complete, rebooting");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "ota finish failed: %s", esp_err_to_name(err));
    }
}

static void wifi_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retries < 5) {
            esp_wifi_connect();
            s_retries++;
            ESP_LOGI(TAG, "retrying connection (%d)", s_retries);
        } else {
            xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retries = 0;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void)
{
    s_wifi_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to %s", WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "failed to connect to %s", WIFI_SSID);
    }
}

static void on_button(button_event_t event) {
    static const supervisor_event_t events[] = {
        [BUTTON_EVENT_SHORT]     = SUPERVISOR_EVENT_SHORT_PRESS,
        [BUTTON_EVENT_LONG]      = SUPERVISOR_EVENT_LONG_PRESS,
        [BUTTON_EVENT_VERY_LONG] = SUPERVISOR_EVENT_VERY_LONG_PRESS,
    };

    supervisor_post(events[event]);
}

static void on_transition(supervisor_state_t entered, supervisor_event_t event) {
    (void)event;

    if (entered == SUPERVISOR_STATE_RECORDING) {
        esp_err_t err = recorder_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "recording did not start: %s", esp_err_to_name(err));
        }
    } else if (entered == SUPERVISOR_STATE_FINALIZING) {
        esp_err_t err = recorder_stop();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "recording did not stop: %s", esp_err_to_name(err));
            supervisor_post(SUPERVISOR_EVENT_FINALIZE_FAILED);
        }
    }
}

static bool check_health(void) {
    recorder_stats_t stats;
    recorder_get_stats(&stats);

    health_inputs_t inputs = {
        .recorder_failed = stats.failed,
        .overflow_count = stats.overflow_count,
        .silent_ms = stats.silent_ms,
    };
    inputs.sd_mounted = sd_card_free_pct(&inputs.free_space_pct) == ESP_OK;

    uint32_t problems = health_evaluate(&inputs);
    ESP_LOGI(TAG, "health: sd %s, free %" PRIu32 "%%, recorder %s, overflows %" PRIu32
             ", silent %" PRIu32 " ms, problems 0x%02" PRIx32,
             inputs.sd_mounted ? "ok" : "missing", inputs.free_space_pct,
             inputs.recorder_failed ? "failed" : "ok", inputs.overflow_count,
             inputs.silent_ms, problems);
    return problems == 0;
}

static void on_recording_finished(esp_err_t result) {
    supervisor_post(result == ESP_OK ? SUPERVISOR_EVENT_FINALIZE_OK : SUPERVISOR_EVENT_FINALIZE_FAILED);
}

#if CONFIG_NOCTURNE_SYSTEM_CHECK
static void run_system_check(void) {
    ESP_ERROR_CHECK(key_manager_wait_ready(KEY_READY_TIMEOUT_MS));

    uint32_t capture_ms = CONFIG_NOCTURNE_SYSTEM_CHECK_CAPTURE_SECONDS * 1000;

    supervisor_post(SUPERVISOR_EVENT_SHORT_PRESS);
    vTaskDelay(pdMS_TO_TICKS(capture_ms / 2));
    supervisor_post(SUPERVISOR_EVENT_SHORT_PRESS);
    vTaskDelay(pdMS_TO_TICKS(capture_ms - capture_ms / 2));
    supervisor_post(SUPERVISOR_EVENT_LONG_PRESS);

    vTaskDelay(pdMS_TO_TICKS(STATE_POLL_MS));
    while (supervisor_state() != SUPERVISOR_STATE_IDLE) {
        vTaskDelay(pdMS_TO_TICKS(STATE_POLL_MS));
    }

    system_check_session();
}
#endif

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(key_manager_init(NOCTURNE_PASSPHRASE));
    ESP_ERROR_CHECK(led_init());
    ESP_ERROR_CHECK(supervisor_init(on_transition, check_health));
    ESP_ERROR_CHECK(button_init(on_button));

    const esp_app_desc_t *app_desc = esp_app_get_description();
    ESP_LOGI(TAG, "running version: %s", app_desc->version);

    wifi_init_sta();

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    
    if (esp_ota_get_state_partition(running, &state) == ESP_OK) {
        if (state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "image pending verify, marking valid");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }

    // Uncomment when finished development
    // do_ota();

    if (sd_card_mount() != ESP_OK) {
        supervisor_post(SUPERVISOR_EVENT_FAULT);
        return;
    }

    ESP_ERROR_CHECK(recorder_init(on_recording_finished));
    supervisor_post(SUPERVISOR_EVENT_BOOT_DONE);

#if CONFIG_NOCTURNE_SYSTEM_CHECK
    run_system_check();
#endif
}
