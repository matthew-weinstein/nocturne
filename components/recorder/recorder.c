#include "recorder.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "audio_encoder.h"
#include "audio_format.h"
#include "backpressure.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2s_microphone.h"
#include "pcm_convert.h"
#include "ring_buffer.h"
#include "session.h"

#define RING_CAPACITY_SAMPLES (AUDIO_FORMAT_SAMPLE_RATE_HZ * 10)
#define BLOCK_SAMPLES         512

#define TASK_STACK_BYTES 16384
#define TASK_PRIORITY    20
#define TASK_CORE        1

#define SILENCE_LOG_MS 30000

static const char *TAG = "recorder";

static int32_t samples[BLOCK_SAMPLES];
static int16_t pcm[BLOCK_SAMPLES];
static int16_t frame[AUDIO_FORMAT_FRAME_SAMPLES];
static uint8_t packet[AUDIO_FORMAT_MAX_PACKET_BYTES];

static TaskHandle_t task;
static recorder_handler_t finished;
static recorder_segment_handler_t segment_closed;
static recorder_backpressure_handler_t backpressure_changed;
static bool backpressure_engaged;
static char resume_dir[SESSION_DIR_LEN];

static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool recording;
static volatile bool stop_requested;

static portMUX_TYPE stats_lock = portMUX_INITIALIZER_UNLOCKED;
static bool failed;
static uint32_t overflow_count;
static uint32_t drop_count;
static int64_t last_sound_us;

static bool has_sound(const int16_t *block, size_t num_samples) {
    for (size_t i = 0; i < num_samples; i++) {
        if (block[i] != 0) {
            return true;
        }
    }
    return false;
}

static void record_block(uint32_t num_overflows, bool heard) {
    taskENTER_CRITICAL(&stats_lock);
    overflow_count += num_overflows;
    if (heard) {
        last_sound_us = esp_timer_get_time();
    }
    taskEXIT_CRITICAL(&stats_lock);
}

static void record_drop(void) {
    taskENTER_CRITICAL(&stats_lock);
    drop_count++;
    taskEXIT_CRITICAL(&stats_lock);
}

static void reset_stats(void) {
    taskENTER_CRITICAL(&stats_lock);
    failed = false;
    overflow_count = 0;
    drop_count = 0;
    last_sound_us = esp_timer_get_time();
    taskEXIT_CRITICAL(&stats_lock);
}

static void mark_failed(void) {
    taskENTER_CRITICAL(&stats_lock);
    failed = true;
    taskEXIT_CRITICAL(&stats_lock);
}

static void set_backpressure(bool engaged) {
    if (engaged == backpressure_engaged) {
        return;
    }
    backpressure_engaged = engaged;
    if (backpressure_changed != NULL) {
        backpressure_changed(engaged);
    }
}

static esp_err_t encode_available(void) {
    while (ring_buffer_available() >= AUDIO_FORMAT_FRAME_SAMPLES) {
        esp_err_t status = ring_buffer_read(frame, AUDIO_FORMAT_FRAME_SAMPLES);
        if (status != ESP_OK) {
            return status;
        }

        size_t num_bytes = 0;
        status = audio_encoder_encode_frame(frame, packet, sizeof(packet), &num_bytes);
        if (status != ESP_OK) {
            ESP_LOGW(TAG, "encoder failed (%s), frame dropped", esp_err_to_name(status));
            record_drop();
            continue;
        }

        status = session_write_packet(packet, num_bytes);
        if (status != ESP_OK) {
            return status;
        }
    }
    return ESP_OK;
}

static uint32_t count_overflows(bool ring_overflowed, uint32_t *mic_overflows_seen) {
    uint32_t mic_overflows = i2s_microphone_overflow_count();
    uint32_t num_overflows = mic_overflows - *mic_overflows_seen;
    *mic_overflows_seen = mic_overflows;

    if (ring_overflowed) {
        num_overflows++;
    }
    if (num_overflows > 0) {
        ESP_LOGW(TAG, "recorder fell behind, %" PRIu32 " overflows, audio dropped", num_overflows);
    }
    return num_overflows;
}

static void log_silence(bool *silence_logged) {
    recorder_stats_t stats;
    recorder_get_stats(&stats);

    if (stats.silent_ms >= SILENCE_LOG_MS && !*silence_logged) {
        ESP_LOGW(TAG, "mic silent for %" PRIu32 " s", stats.silent_ms / 1000);
        *silence_logged = true;
    } else if (stats.silent_ms < SILENCE_LOG_MS && *silence_logged) {
        ESP_LOGI(TAG, "mic heard again");
        *silence_logged = false;
    }
}

static esp_err_t capture(void) {
    uint32_t captured = 0;
    size_t max_occupancy = 0;
    uint32_t num_overflows = 0;
    uint32_t num_engaged = 0;
    uint32_t mic_overflows_seen = 0;
    bool silence_logged = false;
    esp_err_t status = ESP_OK;

    while (!stop_requested && status == ESP_OK) {
        size_t num_samples = 0;
        esp_err_t read_status = i2s_microphone_read(samples, BLOCK_SAMPLES, &num_samples);
        if (read_status != ESP_OK) {
            ESP_LOGW(TAG, "mic read failed (%s), block dropped", esp_err_to_name(read_status));
            record_drop();
            continue;
        }

        pcm_convert_from_i2s(samples, pcm, num_samples);
        if (captured == 0) {
            mic_overflows_seen = i2s_microphone_overflow_count();
        }

        bool ring_overflowed = ring_buffer_write(pcm, num_samples) != ESP_OK;
        uint32_t block_overflows = count_overflows(ring_overflowed, &mic_overflows_seen);
        num_overflows += block_overflows;
        record_block(block_overflows, has_sound(pcm, num_samples));
        log_silence(&silence_logged);
        captured += num_samples;

        size_t occupancy = ring_buffer_available();
        if (occupancy > max_occupancy) {
            max_occupancy = occupancy;
        }

        bool engaged = backpressure_update(backpressure_engaged, occupancy, RING_CAPACITY_SAMPLES);
        if (engaged && !backpressure_engaged) {
            num_engaged++;
        }
        set_backpressure(engaged);

        status = encode_available();
    }

    set_backpressure(false);

    ESP_LOGI(TAG, "captured %" PRIu32 " samples, peak occupancy %u, overflows %" PRIu32 ", uploads paused %" PRIu32
             " times", captured, (unsigned)max_occupancy, num_overflows, num_engaged);
    return status;
}

static esp_err_t record_session(bool *out_session_closed) {
    *out_session_closed = false;

    esp_err_t status = ring_buffer_init(RING_CAPACITY_SAMPLES);
    if (status != ESP_OK) {
        return status;
    }

    status = audio_encoder_init();
    if (status == ESP_OK) {
        if (resume_dir[0] == '\0') {
            status = session_start(segment_closed);
        } else {
            status = session_resume(resume_dir, segment_closed);
        }
    }

    if (status == ESP_OK) {
        status = capture();

        esp_err_t finish_status = session_finish();
        *out_session_closed = finish_status == ESP_OK;
        if (status == ESP_OK) {
            status = finish_status;
        }
    }

    audio_encoder_deinit();
    ring_buffer_deinit();
    return status;
}

static bool end_recording(void) {
    taskENTER_CRITICAL(&state_lock);
    bool stopped = stop_requested;
    recording = false;
    taskEXIT_CRITICAL(&state_lock);
    return stopped;
}

static void recorder_task(void *arg) {
    (void)arg;

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        bool session_closed = false;
        esp_err_t result = record_session(&session_closed);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "recording failed: %s", esp_err_to_name(result));
            mark_failed();
        }

        if (end_recording()) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }

        if (finished != NULL) {
            finished(result, session_closed);
        }
    }
}

esp_err_t recorder_init(recorder_handler_t on_finished, recorder_segment_handler_t on_segment_closed,
                        recorder_backpressure_handler_t on_backpressure) {
    if (task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t status = i2s_microphone_init();
    if (status != ESP_OK) {
        return status;
    }

    finished = on_finished;
    segment_closed = on_segment_closed;
    backpressure_changed = on_backpressure;

    BaseType_t created = xTaskCreatePinnedToCore(recorder_task, "recorder", TASK_STACK_BYTES,
                                                 NULL, TASK_PRIORITY, &task, TASK_CORE);
    if (created != pdPASS) {
        task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t begin(const char *session_dir) {
    taskENTER_CRITICAL(&state_lock);
    bool idle = task != NULL && !recording;
    if (idle) {
        recording = true;
    }
    taskEXIT_CRITICAL(&state_lock);
    if (!idle) {
        return ESP_ERR_INVALID_STATE;
    }

    snprintf(resume_dir, sizeof(resume_dir), "%s", session_dir);
    reset_stats();
    stop_requested = false;
    xTaskNotifyGive(task);
    return ESP_OK;
}

esp_err_t recorder_start(void) {
    return begin("");
}

esp_err_t recorder_resume(const char *session_dir) {
    if (session_dir[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    return begin(session_dir);
}

esp_err_t recorder_stop(void) {
    taskENTER_CRITICAL(&state_lock);
    bool stoppable = task != NULL && recording && !stop_requested;
    if (stoppable) {
        stop_requested = true;
    }
    taskEXIT_CRITICAL(&state_lock);
    if (!stoppable) {
        return ESP_ERR_INVALID_STATE;
    }

    xTaskNotifyGive(task);
    return ESP_OK;
}

void recorder_get_stats(recorder_stats_t *out_stats) {
    taskENTER_CRITICAL(&stats_lock);
    out_stats->failed = failed;
    out_stats->overflow_count = overflow_count;
    out_stats->drop_count = drop_count;
    out_stats->silent_ms = (uint32_t)((esp_timer_get_time() - last_sound_us) / 1000);
    taskEXIT_CRITICAL(&stats_lock);
}
