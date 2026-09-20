#include <string.h>
#include <math.h>
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
#include <inttypes.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2s_microphone.h"
#include "sd_card.h"
#include "wav_writer.h"
#include "audio_encoder.h"

#define SAMPLE_SHIFT 9  // Controls amplitude of records; find optimal value
#define CAPTURE_SECONDS 10

#define BLOCK_SIZE_SAMPLES 512
#define REPORT_INTERVAL_MS 200

static int16_t pcm[BLOCK_SIZE_SAMPLES];
static int32_t samples[BLOCK_SIZE_SAMPLES];

#define TEST_TONE_HZ      440
#define TEST_DURATION_SEC 1
#define TEST_FRAMES       ((MICROPHONE_SAMPLE_RATE_HZ * TEST_DURATION_SEC) / OPUS_FRAME_SIZE_SAMPLES)

static int16_t tone_frame[OPUS_FRAME_SIZE_SAMPLES];
static uint8_t packet[OPUS_MAX_PACKET_BYTES];

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

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_handler, NULL, NULL));

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

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

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





    // Uncomment when finished development?
    // do_ota();





    ESP_ERROR_CHECK(i2s_microphone_init());

    ESP_ERROR_CHECK(sd_card_mount());
    ESP_ERROR_CHECK(audio_encoder_init());

    FILE *file = fopen(SD_CARD_MOUNT_POINT "/test.opusraw", "wb");
    if (file == NULL) {
        printf("could not open output file\n");
        return;
    }

    size_t total_bytes = 0;

    for (int frame = 0; frame < TEST_FRAMES; frame++) {
        for (int i = 0; i < OPUS_FRAME_SIZE_SAMPLES; i++) {
            int sample_index = frame * OPUS_FRAME_SIZE_SAMPLES + i;
            double phase = 2.0 * M_PI * TEST_TONE_HZ * sample_index
                        / MICROPHONE_SAMPLE_RATE_HZ;
            tone_frame[i] = (int16_t)(sin(phase) * 8000.0);
        }

        size_t num_bytes = 0;
        ESP_ERROR_CHECK(audio_encoder_encode_frame(tone_frame, packet,
                                                OPUS_MAX_PACKET_BYTES,
                                                &num_bytes));

        uint16_t length = (uint16_t)num_bytes;
        fwrite(&length, sizeof(length), 1, file);
        fwrite(packet, 1, num_bytes, file);

        total_bytes += num_bytes;
    }

    fclose(file);
    ESP_ERROR_CHECK(audio_encoder_deinit());
    ESP_ERROR_CHECK(sd_card_unmount());

    printf("encoded %d frames, %u bytes, %.1f bytes/frame\n",
        TEST_FRAMES, (unsigned)total_bytes, (float)total_bytes / TEST_FRAMES);
}