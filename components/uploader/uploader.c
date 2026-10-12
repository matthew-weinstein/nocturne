#include "uploader.h"

#include <inttypes.h>
#include <stdio.h>
#include "b2_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "manifest.h"
#include "wifi.h"

#define PATH_LEN     128
#define FILENAME_LEN 32

static const char *TAG = "uploader";

static const uint32_t retry_delays_ms[] = { 1000, 4000, 16000 };
#define NUM_RETRIES (sizeof(retry_delays_ms) / sizeof(retry_delays_ms[0]))

static manifest_t manifest;

static esp_err_t upload_with_retry(const char *path, const char *object_name) {
    esp_err_t err = b2_client_upload_file(path, object_name);

    for (size_t retry = 0; err != ESP_OK && retry < NUM_RETRIES; retry++) {
        ESP_LOGW(TAG, "%s failed (%s), retrying in %" PRIu32 " ms", object_name, esp_err_to_name(err),
                 retry_delays_ms[retry]);
        vTaskDelay(pdMS_TO_TICKS(retry_delays_ms[retry]));
        err = b2_client_upload_file(path, object_name);
    }
    return err;
}

esp_err_t uploader_upload_session(const char *session_dir, int *out_num_uploaded) {
    *out_num_uploaded = 0;

    char manifest_path[PATH_LEN];
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.bin", session_dir);

    esp_err_t err = manifest_open(&manifest, manifest_path);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "could not open %s: %s", manifest_path, esp_err_to_name(err));
        return err;
    }

    for (int index = manifest_next_upload(&manifest); index >= 0; index = manifest_next_upload(&manifest)) {
        if (!wifi_is_connected()) {
            ESP_LOGW(TAG, "no wifi, %s paused at segment %d", manifest.session_id, index);
            return ESP_ERR_INVALID_STATE;
        }

        char filename[FILENAME_LEN];
        manifest_segment_filename(index, filename, sizeof(filename));

        char path[PATH_LEN];
        char object_name[PATH_LEN];
        snprintf(path, sizeof(path), "%s/%s", session_dir, filename);
        snprintf(object_name, sizeof(object_name), "%s/%s", manifest.session_id, filename);

        err = upload_with_retry(path, object_name);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s not uploaded, left queued: %s", object_name, esp_err_to_name(err));
            return err;
        }

        err = manifest_record_upload(&manifest, manifest_path, index);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s uploaded but not journaled: %s", object_name, esp_err_to_name(err));
            return err;
        }
        (*out_num_uploaded)++;
    }

    ESP_LOGI(TAG, "%s: %d uploaded, none pending", manifest.session_id, *out_num_uploaded);
    return ESP_OK;
}
