#include "uploader.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "b2_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
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
#define QUEUE_DEPTH      4
#define FINALIZE_SLOTS   1

#define RUNNING_BIT BIT0

static const char *TAG = "uploader";

static const uint32_t retry_delays_ms[] = { 1000, 4000, 16000 };
#define NUM_RETRIES (sizeof(retry_delays_ms) / sizeof(retry_delays_ms[0]))

static manifest_t manifest;
static uint32_t num_retries;
static char retries_dir[SESSION_DIR_LEN];

typedef struct {
    uploader_job_t job;
    char session_dir[SESSION_DIR_LEN];
    session_json_t metadata;
} uploader_request_t;

static QueueHandle_t requests;
static uploader_request_t request;
static uploader_handler_t handler;
static volatile bool finalizing;
static char json[JSON_MAX_BYTES];
static EventGroupHandle_t run_state;

static portMUX_TYPE pending_lock = portMUX_INITIALIZER_UNLOCKED;
static char pending_dir[SESSION_DIR_LEN];
static int num_pending;

static void wait_while_paused(void) {
    if ((xEventGroupGetBits(run_state) & RUNNING_BIT) != 0) {
        return;
    }

    ESP_LOGI(TAG, "paused for the recorder");
    xEventGroupWaitBits(run_state, RUNNING_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "resumed");
}

static esp_err_t upload_with_retry(const char *path, const char *object_name) {
    wait_while_paused();
    esp_err_t err = b2_client_upload_file(path, object_name);

    for (size_t retry = 0; err != ESP_OK && retry < NUM_RETRIES; retry++) {
        ESP_LOGW(TAG, "%s failed (%s), retrying in %" PRIu32 " ms", object_name, esp_err_to_name(err),
                 retry_delays_ms[retry]);
        vTaskDelay(pdMS_TO_TICKS(retry_delays_ms[retry]));
        num_retries++;
        wait_while_paused();
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

static void publish_pending(const char *session_dir) {
    int count = manifest_num_pending(&manifest);

    taskENTER_CRITICAL(&pending_lock);
    strlcpy(pending_dir, session_dir, sizeof(pending_dir));
    num_pending = count;
    taskEXIT_CRITICAL(&pending_lock);
}

static esp_err_t upload_segments(const char *session_dir, const char *manifest_path, int *out_num_uploaded) {
    *out_num_uploaded = 0;
    publish_pending(session_dir);

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
        publish_pending(session_dir);
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

static esp_err_t write_session_json(const char *path, session_json_t *metadata) {
    metadata->session_id = manifest.session_id;
    metadata->started_unix = manifest.started_unix;
    metadata->segment_count = manifest.num_segments;
    metadata->resume_count = manifest.resume_count;
    metadata->upload_retries = num_retries;

    size_t num_bytes = 0;
    esp_err_t err = session_json_format(metadata, json, sizeof(json), &num_bytes);
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

static esp_err_t upload_pending(const char *session_dir, int *out_num_uploaded) {
    *out_num_uploaded = 0;

    char manifest_path[PATH_LEN];
    esp_err_t err = open_journal(session_dir, manifest_path, sizeof(manifest_path));
    if (err != ESP_OK) {
        return err;
    }
    return upload_segments(session_dir, manifest_path, out_num_uploaded);
}

static esp_err_t finalize(const char *session_dir, session_json_t *metadata, int *out_num_uploaded) {
    *out_num_uploaded = 0;

    char manifest_path[PATH_LEN];
    esp_err_t err = open_journal(session_dir, manifest_path, sizeof(manifest_path));
    if (err != ESP_OK) {
        return err;
    }

    if (wifi_wait_connected(WIFI_TIMEOUT_MS) != ESP_OK) {
        ESP_LOGW(TAG, "no wifi after %d s", WIFI_TIMEOUT_MS / 1000);
    }

    esp_err_t segments_err = upload_segments(session_dir, manifest_path, out_num_uploaded);

    char json_path[PATH_LEN];
    snprintf(json_path, sizeof(json_path), "%s/session.json", session_dir);

    err = write_session_json(json_path, metadata);
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

    err = manifest_record_metadata_upload(&manifest, manifest_path);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s uploaded but not journaled: %s", object_name, esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "%s finalized: %d segments, %" PRIu32 " retries", manifest.session_id, manifest.num_segments,
             num_retries);
    return ESP_OK;
}

static bool delete_next(const cleanup_session_t *session, int64_t now_unix) {
    uint32_t free_pct;
    if (sd_card_free_pct(&free_pct) != ESP_OK || !cleanup_should_delete(session, now_unix, free_pct, RETENTION_SECONDS)) {
        return false;
    }

    wait_while_paused();
    esp_err_t err = cleanup_delete_session(session->dir);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "could not delete %s: %s", session->dir, esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "deleted %s, last written %" PRId64 ", %" PRIu32 "%% free before", session->dir,
             session->last_written_unix, free_pct);
    return true;
}

static esp_err_t clean_up(int *out_num_deleted) {
    static cleanup_scan_t scan;

    *out_num_deleted = 0;
    int64_t now_unix = (int64_t)time(NULL);

    for (;;) {
        esp_err_t err = cleanup_scan(SESSION_ROOT_DIR, &scan);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "cleanup scan failed: %s", esp_err_to_name(err));
            return err;
        }

        int num_listed = cleanup_num_listed(&scan);
        int num_deleted = 0;
        while (num_deleted < num_listed && delete_next(&scan.sessions[num_deleted], now_unix)) {
            num_deleted++;
        }

        *out_num_deleted += num_deleted;
        if (num_deleted < num_listed || num_listed == scan.num_deletable) {
            break;
        }
    }

    uint32_t free_pct = 0;
    (void)sd_card_free_pct(&free_pct);
    ESP_LOGI(TAG, "cleanup: %d sessions deleted, %" PRIu32 "%% free", *out_num_deleted, free_pct);
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
        xQueueReceive(requests, &request, portMAX_DELAY);

        if (strcmp(request.session_dir, retries_dir) != 0) {
            num_retries = 0;
            snprintf(retries_dir, sizeof(retries_dir), "%s", request.session_dir);
        }

        int num_uploaded = 0;
        esp_err_t result;
        if (request.job == UPLOADER_JOB_FINALIZE || request.job == UPLOADER_JOB_RECOVER) {
            result = finalize(request.session_dir, &request.metadata, &num_uploaded);
            if (result != ESP_OK) {
                ESP_LOGE(TAG, "finalize failed: %s", esp_err_to_name(result));
            }
            if (request.job == UPLOADER_JOB_FINALIZE) {
                finalizing = false;
            }
        } else {
            result = upload_pending(request.session_dir, &num_uploaded);
        }

        if (handler != NULL) {
            handler(request.job, result, num_uploaded);
        }
    }
}

esp_err_t uploader_init(uploader_handler_t on_done) {
    if (requests != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    run_state = xEventGroupCreate();
    if (run_state == NULL) {
        return ESP_ERR_NO_MEM;
    }
    xEventGroupSetBits(run_state, RUNNING_BIT);

    requests = xQueueCreate(QUEUE_DEPTH, sizeof(uploader_request_t));
    if (requests == NULL) {
        vEventGroupDelete(run_state);
        run_state = NULL;
        return ESP_ERR_NO_MEM;
    }

    handler = on_done;
    b2_client_set_block_hook(wait_while_paused);

    BaseType_t created = xTaskCreatePinnedToCore(uploader_task, "uploader", TASK_STACK_BYTES,
                                                 NULL, TASK_PRIORITY, NULL, TASK_CORE);
    if (created != pdPASS) {
        b2_client_set_block_hook(NULL);
        vQueueDelete(requests);
        requests = NULL;
        vEventGroupDelete(run_state);
        run_state = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t post(const uploader_request_t *queued) {
    if (xQueueSend(requests, queued, 0) != pdTRUE) {
        ESP_LOGW(TAG, "request queue full, %s dropped", queued->session_dir);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t post_leaving_finalize_slot(const uploader_request_t *queued) {
    if (requests == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (uxQueueSpacesAvailable(requests) <= FINALIZE_SLOTS) {
        ESP_LOGW(TAG, "request queue nearly full, %s dropped", queued->session_dir);
        return ESP_ERR_NO_MEM;
    }
    return post(queued);
}

esp_err_t uploader_upload_pending(const char *session_dir) {
    uploader_request_t queued = { .job = UPLOADER_JOB_PENDING };
    snprintf(queued.session_dir, sizeof(queued.session_dir), "%s", session_dir);
    return post_leaving_finalize_slot(&queued);
}

esp_err_t uploader_recover_session(const char *session_dir, const session_json_t *session) {
    uploader_request_t queued = { .job = UPLOADER_JOB_RECOVER, .metadata = *session };
    snprintf(queued.session_dir, sizeof(queued.session_dir), "%s", session_dir);
    return post_leaving_finalize_slot(&queued);
}

esp_err_t uploader_finalize_session(const char *session_dir, const session_json_t *session) {
    if (requests == NULL || finalizing) {
        return ESP_ERR_INVALID_STATE;
    }

    uploader_request_t queued = { .job = UPLOADER_JOB_FINALIZE, .metadata = *session };
    snprintf(queued.session_dir, sizeof(queued.session_dir), "%s", session_dir);

    finalizing = true;
    esp_err_t err = post(&queued);
    if (err != ESP_OK) {
        finalizing = false;
    }
    return err;
}

esp_err_t uploader_set_paused(bool paused) {
    if (run_state == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (paused) {
        xEventGroupClearBits(run_state, RUNNING_BIT);
    } else {
        xEventGroupSetBits(run_state, RUNNING_BIT);
    }
    return ESP_OK;
}

int uploader_pending_segments(const char *session_dir) {
    taskENTER_CRITICAL(&pending_lock);
    int count = (strcmp(pending_dir, session_dir) == 0) ? num_pending : 0;
    taskEXIT_CRITICAL(&pending_lock);
    return count;
}
