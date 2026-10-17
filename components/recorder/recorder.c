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
static volatile bool recording;
static volatile bool stop_requested;

static portMUX_TYPE stats_lock = portMUX_INITIALIZER_UNLOCKED;
static bool failed;
static uint32_t overflow_count;
static int64_t last_sound_us;

static bool has_sound(const int16_t *block, size_t num_samples) {
    for (size_t i = 0; i < num_samples; i++) {
        if (block[i] != 0) {
            return true;
        }
    }
    return false;
}

static void record_block(bool overflowed, bool heard) {
    taskENTER_CRITICAL(&stats_lock);
    if (overflowed) {
        overflow_count++;
    }
    if (heard) {
        last_sound_us = esp_timer_get_time();
    }
    taskEXIT_CRITICAL(&stats_lock);
}

static void reset_stats(void) {
    taskENTER_CRITICAL(&stats_lock);
    failed = false;
    overflow_count = 0;
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
            return status;
        }

        status = session_write_packet(packet, num_bytes);
        if (status != ESP_OK) {
            return status;
        }
    }
    return ESP_OK;
}

static esp_err_t capture(void) {
    uint32_t captured = 0;
    size_t max_occupancy = 0;
    uint32_t num_overflows = 0;
    uint32_t num_engaged = 0;
    esp_err_t status = ESP_OK;

    while (!stop_requested && status == ESP_OK) {
        size_t num_samples = 0;
        if (i2s_microphone_read(samples, BLOCK_SAMPLES, &num_samples) != ESP_OK) {
            continue;
        }

        pcm_convert_from_i2s(samples, pcm, num_samples);

        bool overflowed = ring_buffer_write(pcm, num_samples) != ESP_OK;
        if (overflowed) {
            num_overflows++;
        }
        record_block(overflowed, has_sound(pcm, num_samples));
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

    ESP_LOGI(TAG, "captured %" PRIu32 " samples, peak occupancy %u, overflows %u, uploads paused %" PRIu32 " times",
             captured, (unsigned)max_occupancy, (unsigned)num_overflows, num_engaged);
    return status;
}

static esp_err_t record_session(void) {
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
        if (status == ESP_OK) {
            status = finish_status;
        }
    }

    audio_encoder_deinit();
    ring_buffer_deinit();
    return status;
}

static void recorder_task(void *arg) {
    (void)arg;

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        esp_err_t result = record_session();
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "recording failed: %s", esp_err_to_name(result));
            mark_failed();
        }

        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        recording = false;
        if (finished != NULL) {
            finished(result);
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
    if (task == NULL || recording) {
        return ESP_ERR_INVALID_STATE;
    }

    snprintf(resume_dir, sizeof(resume_dir), "%s", session_dir);
    reset_stats();
    stop_requested = false;
    recording = true;
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
    if (task == NULL || !recording || stop_requested) {
        return ESP_ERR_INVALID_STATE;
    }

    stop_requested = true;
    xTaskNotifyGive(task);
    return ESP_OK;
}

void recorder_get_stats(recorder_stats_t *out_stats) {
    taskENTER_CRITICAL(&stats_lock);
    out_stats->failed = failed;
    out_stats->overflow_count = overflow_count;
    out_stats->silent_ms = (uint32_t)((esp_timer_get_time() - last_sound_us) / 1000);
    taskEXIT_CRITICAL(&stats_lock);
}
