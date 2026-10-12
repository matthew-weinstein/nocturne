#include "uploader.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include "b2_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "manifest.h"
#include "wifi.h"

#define SESSION_DIR_LEN 64
#define PATH_LEN        128
#define FILENAME_LEN    32
#define JSON_MAX_BYTES  1024

#define WIFI_TIMEOUT_MS 30000

#define TASK_STACK_BYTES 12288
#define TASK_PRIORITY    5
#define TASK_CORE        0

static const char *TAG = "uploader";

static const uint32_t retry_delays_ms[] = { 1000, 4000, 16000 };
#define NUM_RETRIES (sizeof(retry_delays_ms) / sizeof(retry_delays_ms[0]))

static manifest_t manifest;
static uint32_t num_retries;

static TaskHandle_t task;
static uploader_handler_t finalized;
static volatile bool finalizing;
static char finalize_dir[SESSION_DIR_LEN];
static session_json_t finalize_metadata;
static char json[JSON_MAX_BYTES];

static esp_err_t upload_with_retry(const char *path, const char *object_name) {
    esp_err_t err = b2_client_upload_file(path, object_name);

    for (size_t retry = 0; err != ESP_OK && retry < NUM_RETRIES; retry++) {
        ESP_LOGW(TAG, "%s failed (%s), retrying in %" PRIu32 " ms", object_name, esp_err_to_name(err),
                 retry_delays_ms[retry]);
        vTaskDelay(pdMS_TO_TICKS(retry_delays_ms[retry]));
        num_retries++;
        err = b2_client_upload_file(path, object_name);
    }
    return err;
}

static esp_err_t open_journal(const char *session_dir, char *manifest_path, size_t max_path_bytes) {
    snprintf(manifest_path, max_path_bytes, "%s/manifest.bin", session_dir);

    esp_err_t err = manifest_open(&manifest, manifest_path);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "could not open %s: %s", manifest_path, esp_err_to_name(err));
    }
    return err;
}

static esp_err_t upload_segments(const char *session_dir, const char *manifest_path, int *out_num_uploaded) {
    *out_num_uploaded = 0;

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

        esp_err_t err = upload_with_retry(path, object_name);
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

static esp_err_t write_file(const char *path, const char *data, size_t num_bytes) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return ESP_FAIL;
    }

    bool written = fwrite(data, 1, num_bytes, file) == num_bytes && fflush(file) == 0 && fsync(fileno(file)) == 0;
    fclose(file);
    return written ? ESP_OK : ESP_FAIL;
}

static esp_err_t write_session_json(const char *path) {
    finalize_metadata.session_id = manifest.session_id;
    finalize_metadata.started_unix = manifest.started_unix;
    finalize_metadata.segment_count = manifest.num_segments;
    finalize_metadata.upload_retries = num_retries;

    size_t num_bytes = 0;
    esp_err_t err = session_json_format(&finalize_metadata, json, sizeof(json), &num_bytes);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "session.json does not fit: %s", esp_err_to_name(err));
        return err;
    }

    err = write_file(path, json, num_bytes);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "could not write %s", path);
    }
    return err;
}

static esp_err_t finalize(void) {
    num_retries = 0;

    char manifest_path[PATH_LEN];
    esp_err_t err = open_journal(finalize_dir, manifest_path, sizeof(manifest_path));
    if (err != ESP_OK) {
        return err;
    }

    if (wifi_wait_connected(WIFI_TIMEOUT_MS) != ESP_OK) {
        ESP_LOGW(TAG, "no wifi after %d s", WIFI_TIMEOUT_MS / 1000);
    }

    int num_uploaded = 0;
    esp_err_t segments_err = upload_segments(finalize_dir, manifest_path, &num_uploaded);

    char json_path[PATH_LEN];
    snprintf(json_path, sizeof(json_path), "%s/session.json", finalize_dir);

    err = write_session_json(json_path);
    if (err != ESP_OK) {
        return err;
    }
    if (segments_err != ESP_OK) {
        return segments_err;
    }

    char object_name[PATH_LEN];
    snprintf(object_name, sizeof(object_name), "%s/session.json", manifest.session_id);

    err = upload_with_retry(json_path, object_name);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s not uploaded: %s", object_name, esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "%s finalized: %d segments, %" PRIu32 " retries", manifest.session_id, manifest.num_segments,
             num_retries);
    return ESP_OK;
}

static void prepare_upload(void) {
    if (wifi_wait_connected(WIFI_TIMEOUT_MS) != ESP_OK) {
        ESP_LOGW(TAG, "no wifi, b2 not ready");
        return;
    }

    esp_err_t err = b2_client_get_upload_url();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "b2 not ready: %s", esp_err_to_name(err));
    }
}

static void uploader_task(void *arg) {
    (void)arg;

    prepare_upload();

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        esp_err_t result = finalize();
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "finalize failed: %s", esp_err_to_name(result));
        }

        finalizing = false;
        if (finalized != NULL) {
            finalized(result);
        }
    }
}

esp_err_t uploader_init(uploader_handler_t on_finalized) {
    if (task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    finalized = on_finalized;

    BaseType_t created = xTaskCreatePinnedToCore(uploader_task, "uploader", TASK_STACK_BYTES,
                                                 NULL, TASK_PRIORITY, &task, TASK_CORE);
    if (created != pdPASS) {
        task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t uploader_finalize_session(const char *session_dir, const session_json_t *session) {
    if (task == NULL || finalizing) {
        return ESP_ERR_INVALID_STATE;
    }

    snprintf(finalize_dir, sizeof(finalize_dir), "%s", session_dir);
    finalize_metadata = *session;
    finalizing = true;
    xTaskNotifyGive(task);
    return ESP_OK;
}

esp_err_t uploader_upload_session(const char *session_dir, int *out_num_uploaded) {
    *out_num_uploaded = 0;

    char manifest_path[PATH_LEN];
    esp_err_t err = open_journal(session_dir, manifest_path, sizeof(manifest_path));
    if (err != ESP_OK) {
        return err;
    }
    return upload_segments(session_dir, manifest_path, out_num_uploaded);
}
