#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "secrets.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_mac.h"
#include "sd_card.h"
#include "session.h"
#include "recovery.h"
#include "recorder.h"
#include "sdkconfig.h"
#include "system_check.h"
#include "key_manager.h"
#include "led.h"
#include "button.h"
#include "supervisor.h"
#include "health.h"
#include "wifi.h"
#include "sntp_time.h"
#include "b2_client.h"
#include "uploader.h"

#define STATE_POLL_MS        100
#define OTA_WIFI_TIMEOUT_MS  30000
#define SNTP_TIMEOUT_MS      30000
#define DEVICE_ID_LEN        13

#define RESUME_WINDOW_SECONDS (CONFIG_NOCTURNE_RESUME_WINDOW_MINUTES * 60)

#if CONFIG_NOCTURNE_SYSTEM_CHECK
#define RESUME_ENABLED false
#else
#define RESUME_ENABLED true
#endif

#if CONFIG_NOCTURNE_OPUS_APPLICATION_AUDIO
#define OPUS_APPLICATION_NAME "audio"
#else
#define OPUS_APPLICATION_NAME "voip"
#endif

static const char *TAG = "nocturne";

static char device_id[DEVICE_ID_LEN];
static bool session_time_reliable;
static uint32_t session_wifi_disconnects_at_start;

typedef struct {
    char dir[RECOVERY_DIR_LEN];
    int64_t ended_unix;
} finished_session_t;

static finished_session_t finished_sessions[RECOVERY_MAX_SESSIONS];
static int num_finished_sessions;
static int next_finished_session;
static volatile bool recovery_uploading;
static char resume_dir[SESSION_DIR_LEN];

#define OTA_URL "https://github.com/matthew-weinstein/nocturne/releases/latest/download/nocturne.bin"

static void do_ota(void)
{
    if (wifi_wait_connected(OTA_WIFI_TIMEOUT_MS) != ESP_OK) {
        ESP_LOGW(TAG, "no wifi, skipping ota check");
        return;
    }

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

static void on_button(button_event_t event) {
    static const supervisor_event_t events[] = {
        [BUTTON_EVENT_SHORT]     = SUPERVISOR_EVENT_SHORT_PRESS,
        [BUTTON_EVENT_LONG]      = SUPERVISOR_EVENT_LONG_PRESS,
        [BUTTON_EVENT_VERY_LONG] = SUPERVISOR_EVENT_VERY_LONG_PRESS,
    };

    supervisor_post(events[event]);
}

static void note_session_start(void) {
    session_time_reliable = sntp_time_is_reliable();
    session_wifi_disconnects_at_start = wifi_disconnect_count();
}

static void on_transition(supervisor_state_t entered, supervisor_event_t event) {
    if (entered == SUPERVISOR_STATE_RESUMING) {
        note_session_start();

        esp_err_t err = recorder_resume(resume_dir);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s did not resume: %s", resume_dir, esp_err_to_name(err));
            supervisor_post(SUPERVISOR_EVENT_BOOT_DONE);
            return;
        }
        supervisor_post(SUPERVISOR_EVENT_RESUMED);
    } else if (entered == SUPERVISOR_STATE_RECORDING && event != SUPERVISOR_EVENT_RESUMED) {
        note_session_start();

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
    inputs.upload_queue_depth = (uint32_t)uploader_pending_segments(session_current_dir());
    inputs.wifi_reconnecting = !wifi_is_connected();

    uint32_t problems = health_evaluate(&inputs);
    ESP_LOGI(TAG, "health: sd %s, free %" PRIu32 "%%, recorder %s, overflows %" PRIu32
             ", silent %" PRIu32 " ms, upload queue %" PRIu32 ", wifi %s, problems 0x%02" PRIx32,
             inputs.sd_mounted ? "ok" : "missing", inputs.free_space_pct,
             inputs.recorder_failed ? "failed" : "ok", inputs.overflow_count,
             inputs.silent_ms, inputs.upload_queue_depth,
             inputs.wifi_reconnecting ? "reconnecting" : "up", problems);
    return problems == 0;
}

static void on_segment_closed(const char *session_dir) {
    uploader_upload_pending(session_dir);
}

static void on_backpressure(bool engaged) {
    uploader_set_paused(engaged);
}

static session_json_t device_metadata(void) {
    session_json_t metadata = {
        .device_id = device_id,
        .firmware_version = esp_app_get_description()->version,
        .opus_application = OPUS_APPLICATION_NAME,
        .bitrate = CONFIG_NOCTURNE_OPUS_BITRATE,
    };
    return metadata;
}

static void on_recording_finished(esp_err_t result) {
    if (result != ESP_OK) {
        supervisor_post(SUPERVISOR_EVENT_FINALIZE_FAILED);
        return;
    }

    recorder_stats_t stats;
    recorder_get_stats(&stats);

    session_json_t metadata = device_metadata();
    metadata.ended_unix = (int64_t)time(NULL);
    metadata.time_reliable = session_time_reliable;
    metadata.buffer_overflows = stats.overflow_count;
    metadata.wifi_disconnects = wifi_disconnect_count() - session_wifi_disconnects_at_start;

    esp_err_t err = uploader_finalize_session(session_current_dir(), &metadata);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "finalize did not start: %s", esp_err_to_name(err));
        supervisor_post(SUPERVISOR_EVENT_FINALIZE_FAILED);
    }
}

static void upload_next_finished_session(void) {
    if (recovery_uploading || next_finished_session >= num_finished_sessions) {
        return;
    }

    const finished_session_t *finished = &finished_sessions[next_finished_session];
    session_json_t metadata = device_metadata();
    metadata.ended_unix = finished->ended_unix;
    metadata.recovered = true;

    recovery_uploading = true;
    next_finished_session++;
    if (uploader_recover_session(finished->dir, &metadata) != ESP_OK) {
        next_finished_session--;
        recovery_uploading = false;
    }
}

static void on_upload_done(uploader_job_t job, esp_err_t result, int num_uploaded) {
    if (job == UPLOADER_JOB_RECOVER) {
        recovery_uploading = false;
    }
    upload_next_finished_session();

    if (job == UPLOADER_JOB_FINALIZE) {
        supervisor_post(result == ESP_OK ? SUPERVISOR_EVENT_FINALIZE_OK : SUPERVISOR_EVENT_FINALIZE_FAILED);
        return;
    }
    if (job == UPLOADER_JOB_RECOVER) {
        return;
    }
#if CONFIG_NOCTURNE_SYSTEM_CHECK
    system_check_pending_done(result, num_uploaded);
#else
    (void)num_uploaded;
#endif
}

static bool recovery_time_reliable(void) {
    if (sntp_time_is_reliable() || sntp_time_survived_reset()) {
        return true;
    }

    ESP_LOGI(TAG, "waiting up to %d s for sntp", SNTP_TIMEOUT_MS / 1000);
    if (sntp_time_wait_reliable(SNTP_TIMEOUT_MS) != ESP_OK) {
        ESP_LOGW(TAG, "time unreliable, no session will resume");
        return false;
    }
    return true;
}

static void finish_session(const recovery_session_t *session) {
    finished_session_t *finished = &finished_sessions[num_finished_sessions];

    esp_err_t err = recovery_finish(session, &finished->ended_unix);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s not finished: %s", session->dir, esp_err_to_name(err));
        return;
    }

    snprintf(finished->dir, sizeof(finished->dir), "%s", session->dir);
    num_finished_sessions++;
    ESP_LOGI(TAG, "finished %s, ended %" PRId64, finished->dir, finished->ended_unix);
}

static bool recover_sessions(void) {
    static recovery_scan_t scan;

    esp_err_t err = recovery_scan(SESSION_ROOT_DIR, &scan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "session scan failed: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "%d incomplete sessions, %d unreadable", scan.num_incomplete, scan.num_unreadable);
    if (recovery_num_listed(&scan) == 0) {
        return false;
    }

    bool time_reliable = recovery_time_reliable();
    int64_t now_unix = (int64_t)time(NULL);

    for (int i = 0; i < recovery_num_listed(&scan); i++) {
        const recovery_session_t *session = &scan.sessions[i];
        ESP_LOGI(TAG, "incomplete: %s, %d segments, last written %" PRId64 ", %" PRId64 " s ago",
                 session->dir, session->num_segments, session->last_written_unix,
                 now_unix - session->last_written_unix);

        if (i == 0 && RESUME_ENABLED
            && recovery_should_resume(session, now_unix, time_reliable, RESUME_WINDOW_SECONDS)) {
            snprintf(resume_dir, sizeof(resume_dir), "%s", session->dir);
            ESP_LOGI(TAG, "resuming %s", resume_dir);
            continue;
        }
        finish_session(session);
    }

    upload_next_finished_session();
    return resume_dir[0] != '\0';
}

static void read_device_id(void) {
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    snprintf(device_id, sizeof(device_id), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

#if CONFIG_NOCTURNE_SYSTEM_CHECK
_Static_assert(CONFIG_NOCTURNE_SYSTEM_CHECK_WIFI_OUTAGE_SECONDS < CONFIG_NOCTURNE_SYSTEM_CHECK_CAPTURE_SECONDS,
               "the WiFi outage must end before the capture does");

static void wait_until(TickType_t start, uint32_t ms) {
    TickType_t wake = start;
    xTaskDelayUntil(&wake, pdMS_TO_TICKS(ms));
}

static void run_system_check(void) {
    ESP_ERROR_CHECK(key_manager_wait_ready());

    uint32_t capture_ms = CONFIG_NOCTURNE_SYSTEM_CHECK_CAPTURE_SECONDS * 1000;
    uint32_t outage_ms = CONFIG_NOCTURNE_SYSTEM_CHECK_WIFI_OUTAGE_SECONDS * 1000;
    uint32_t health_check_ms = capture_ms / 2;
    TickType_t start = xTaskGetTickCount();

    supervisor_post(SUPERVISOR_EVENT_SHORT_PRESS);
    if (outage_ms > 0) {
        wait_until(start, health_check_ms - outage_ms / 2);
        system_check_wifi_outage(true);
    }
    wait_until(start, health_check_ms);
    supervisor_post(SUPERVISOR_EVENT_SHORT_PRESS);
    if (outage_ms > 0) {
        wait_until(start, health_check_ms + outage_ms / 2);
        system_check_wifi_outage(false);
    }
    wait_until(start, capture_ms);
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
    read_device_id();
    ESP_ERROR_CHECK(led_init());
    ESP_ERROR_CHECK(supervisor_init(on_transition, check_health));
    ESP_ERROR_CHECK(button_init(on_button));

    const esp_app_desc_t *app_desc = esp_app_get_description();
    ESP_LOGI(TAG, "running version: %s", app_desc->version);

    ESP_ERROR_CHECK(wifi_init(WIFI_SSID, WIFI_PASS));
    ESP_ERROR_CHECK(sntp_time_init());
    ESP_ERROR_CHECK(b2_client_init(B2_KEY_ID, B2_APP_KEY));

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

    ESP_ERROR_CHECK(recorder_init(on_recording_finished, on_segment_closed, on_backpressure));
    ESP_ERROR_CHECK(uploader_init(on_upload_done));
    bool resuming = recover_sessions();
    supervisor_post(resuming ? SUPERVISOR_EVENT_RESUME_FOUND : SUPERVISOR_EVENT_BOOT_DONE);

#if CONFIG_NOCTURNE_SYSTEM_CHECK
    run_system_check();
    system_check_upload();
#endif
}
