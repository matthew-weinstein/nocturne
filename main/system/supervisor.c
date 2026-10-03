#include "supervisor.h"
#include <stddef.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define QUEUE_LENGTH 8

#define TASK_STACK_BYTES 4096
#define TASK_PRIORITY    15
#define TASK_CORE        0

static const char *TAG = "supervisor";

static const char *const state_names[] = {
    [SUPERVISOR_STATE_BOOT]       = "BOOT",
    [SUPERVISOR_STATE_IDLE]       = "IDLE",
    [SUPERVISOR_STATE_RECORDING]  = "RECORDING",
    [SUPERVISOR_STATE_FINALIZING] = "FINALIZING",
    [SUPERVISOR_STATE_RESUMING]   = "RESUMING",
    [SUPERVISOR_STATE_FAULT]      = "FAULT",
};

static const char *const event_names[] = {
    [SUPERVISOR_EVENT_BOOT_DONE]       = "boot done",
    [SUPERVISOR_EVENT_RESUME_FOUND]    = "resume found",
    [SUPERVISOR_EVENT_RESUMED]         = "resumed",
    [SUPERVISOR_EVENT_SHORT_PRESS]     = "short press",
    [SUPERVISOR_EVENT_LONG_PRESS]      = "long press",
    [SUPERVISOR_EVENT_VERY_LONG_PRESS] = "very long press",
    [SUPERVISOR_EVENT_FINALIZE_OK]     = "finalize ok",
    [SUPERVISOR_EVENT_FINALIZE_FAILED] = "finalize failed",
    [SUPERVISOR_EVENT_FAULT]           = "fault",
};

static QueueHandle_t events;
static volatile supervisor_state_t state = SUPERVISOR_STATE_BOOT;

static supervisor_state_t next_state(supervisor_state_t current, supervisor_event_t event) {
    if (event == SUPERVISOR_EVENT_FAULT) {
        return SUPERVISOR_STATE_FAULT;
    }

    switch (current) {
    case SUPERVISOR_STATE_BOOT:
        if (event == SUPERVISOR_EVENT_BOOT_DONE) {
            return SUPERVISOR_STATE_IDLE;
        }
        if (event == SUPERVISOR_EVENT_RESUME_FOUND) {
            return SUPERVISOR_STATE_RESUMING;
        }
        break;
    case SUPERVISOR_STATE_IDLE:
        if (event == SUPERVISOR_EVENT_SHORT_PRESS) {
            return SUPERVISOR_STATE_RECORDING;
        }
        break;
    case SUPERVISOR_STATE_RECORDING:
        if (event == SUPERVISOR_EVENT_LONG_PRESS) {
            return SUPERVISOR_STATE_FINALIZING;
        }
        break;
    case SUPERVISOR_STATE_FINALIZING:
        if (event == SUPERVISOR_EVENT_FINALIZE_OK || event == SUPERVISOR_EVENT_FINALIZE_FAILED) {
            return SUPERVISOR_STATE_IDLE;
        }
        break;
    case SUPERVISOR_STATE_RESUMING:
        if (event == SUPERVISOR_EVENT_RESUMED) {
            return SUPERVISOR_STATE_RECORDING;
        }
        break;
    case SUPERVISOR_STATE_FAULT:
        break;
    }
    return current;
}

static void handle_in_place(supervisor_state_t current, supervisor_event_t event) {
    if (current == SUPERVISOR_STATE_RECORDING && event == SUPERVISOR_EVENT_SHORT_PRESS) {
        ESP_LOGI(TAG, "health check requested");
    } else if (current == SUPERVISOR_STATE_IDLE && event == SUPERVISOR_EVENT_VERY_LONG_PRESS) {
        ESP_LOGW(TAG, "factory reset requested");
    } else {
        ESP_LOGD(TAG, "%s ignored in %s", event_names[event], state_names[current]);
    }
}

static void supervisor_task(void *arg) {
    (void)arg;

    supervisor_event_t event;

    for (;;) {
        xQueueReceive(events, &event, portMAX_DELAY);

        supervisor_state_t current = state;
        supervisor_state_t next = next_state(current, event);

        if (next == current) {
            handle_in_place(current, event);
            continue;
        }

        ESP_LOGI(TAG, "%s -> %s (%s)", state_names[current], state_names[next], event_names[event]);
        state = next;
    }
}

esp_err_t supervisor_init(void) {
    if (events != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    events = xQueueCreate(QUEUE_LENGTH, sizeof(supervisor_event_t));
    if (events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    BaseType_t created = xTaskCreatePinnedToCore(supervisor_task, "supervisor", TASK_STACK_BYTES,
                                                 NULL, TASK_PRIORITY, NULL, TASK_CORE);
    if (created != pdPASS) {
        vQueueDelete(events);
        events = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t supervisor_post(supervisor_event_t event) {
    if (events == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueueSend(events, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "queue full, dropped %s", event_names[event]);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

supervisor_state_t supervisor_state(void) {
    return state;
}
