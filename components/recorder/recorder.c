#include "recorder.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "audio_encoder.h"
#include "audio_format.h"
#include "esp_log.h"
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
static volatile bool recording;
static volatile bool stop_requested;

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
    size_t overflow_count = 0;
    esp_err_t status = ESP_OK;

    while (!stop_requested && status == ESP_OK) {
        size_t num_samples = 0;
        if (i2s_microphone_read(samples, BLOCK_SAMPLES, &num_samples) != ESP_OK) {
            continue;
        }

        pcm_convert_from_i2s(samples, pcm, num_samples);

        if (ring_buffer_write(pcm, num_samples) != ESP_OK) {
            overflow_count++;
        }
        captured += num_samples;

        size_t occupancy = ring_buffer_available();
        if (occupancy > max_occupancy) {
            max_occupancy = occupancy;
        }

        status = encode_available();
    }

    ESP_LOGI(TAG, "captured %" PRIu32 " samples, peak occupancy %u, overflows %u",
             captured, (unsigned)max_occupancy, (unsigned)overflow_count);
    return status;
}

static esp_err_t record_session(void) {
    esp_err_t status = ring_buffer_init(RING_CAPACITY_SAMPLES);
    if (status != ESP_OK) {
        return status;
    }

    status = audio_encoder_init();
    if (status == ESP_OK) {
        status = session_start();
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
        }

        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        recording = false;
        if (finished != NULL) {
            finished(result);
        }
    }
}

esp_err_t recorder_init(recorder_handler_t on_finished) {
    if (task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t status = i2s_microphone_init();
    if (status != ESP_OK) {
        return status;
    }

    finished = on_finished;

    BaseType_t created = xTaskCreatePinnedToCore(recorder_task, "recorder", TASK_STACK_BYTES,
                                                 NULL, TASK_PRIORITY, &task, TASK_CORE);
    if (created != pdPASS) {
        task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t recorder_start(void) {
    if (task == NULL || recording) {
        return ESP_ERR_INVALID_STATE;
    }

    stop_requested = false;
    recording = true;
    xTaskNotifyGive(task);
    return ESP_OK;
}

esp_err_t recorder_stop(void) {
    if (task == NULL || !recording || stop_requested) {
        return ESP_ERR_INVALID_STATE;
    }

    stop_requested = true;
    xTaskNotifyGive(task);
    return ESP_OK;
}
